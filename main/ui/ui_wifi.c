#include "ui_wifi.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_async.h"
#include "net_store.h"
#include "wifi_net.h"

/*
 * 配网页。整个联网对练里唯一需要用户在电阻屏上敲字的地方，体验成败在这里。
 *
 * 4 个槽位的取舍见 net_store.h：存储不是约束，输入痛苦才是。所以这一页最
 * 重要的功能其实是「从 FMO 伴侣导入」——两个固件刷在同一块板上、共享 NVS
 * 分区，对方配过的 WiFi 可以直接搬过来，绝大多数情况下一个字都不用敲。
 *
 * 横屏 320x240 下键盘弹出会占掉大半屏，所以编辑态是独立一屏（不是弹层），
 * textarea 放在键盘上方——fmo-companion 那边是竖屏布局，不能照搬。
 */

#define SCAN_MAX 20

static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_ta_ssid, *s_ta_pass, *s_kb, *s_dd;
static int       s_edit_idx = -1;
static char      s_scanned[SCAN_MAX][33];
static int       s_scan_n;
/* 扫描是异步的，页面可能在扫描返回前就被删掉了。用代次而不是只判空指针：
   连续两次进出编辑页时，第一次的扫描任务可能在第二次建好页之后才回来，
   那时 s_scr 非空但已经是另一个页面了，直接往里塞结果就是张冠李戴。 */
static volatile uint32_t s_scan_gen;

static lv_obj_t *build_list(void);

/*
 * 输入类控件的统一尺寸。UI_FONT_CN 行高 26，加上下 padding 3+3 和 border 1+1
 * 正好 34——项目里 ui_button() 用 36 是同一笔账。
 *
 * 这个数不能拍脑袋往小了写：对象比内容行高矮时 LVGL 会判定为溢出，既把文字
 * 往下偏移又给控件打开滚动，看起来就是"输入框只有一半高度"。ui_common.c 里
 * 顶栏返回箭头的注释记的是同一个坑。
 */
#define FIELD_H 34

static void field_style(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_top(o, 3, 0);
    lv_obj_set_style_pad_bottom(o, 3, 0);
    lv_obj_set_style_pad_left(o, 6, 0);
    lv_obj_set_style_pad_right(o, 6, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, UI_BLACK, 0);
    lv_obj_set_style_bg_color(o, UI_WHITE, 0);
}

static void show(lv_obj_t *(*builder)(void)) { ui_nav_show(builder); }

/* ---------------------------------------------------------------- 编辑态 */

static void save_cb(lv_event_t *e)
{
    (void)e;
    if (s_edit_idx < 0) return;
    cw_net_cfg_t *cfg = net_store_get();
    cw_wifi_profile_t *p = &cfg->profiles[s_edit_idx];
    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);

    memset(p, 0, sizeof *p);
    if (ssid && ssid[0]) {
        snprintf(p->ssid, sizeof p->ssid, "%s", ssid);
        snprintf(p->password, sizeof p->password, "%s", pass ? pass : "");
        p->used = 1;
    }
    /* SSID 留空 = 清空该槽位，这是删除一个网络的唯一入口 */
    net_store_save();
    show(build_list);
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    lv_textarea_set_text(s_ta_ssid, "");
    lv_textarea_set_text(s_ta_pass, "");
}

static void ta_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_kb, ta);
    /* SSID 多为英文数字，密码同理；统一小写字母键盘，用户可自己切大写。 */
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

/* 从扫描列表选中一个 SSID 后回填。若该 SSID 已存在于某个槽位，连密码一起
   回填——省掉重复输入，也让「改密码」变成「重选一次再改」。 */
static void pick_ssid(lv_event_t *e)
{
    /* 选中项必须从下拉框本身读。之前这里读的是 add_event_cb 传进来的
       user_data，而那是建控件时写死的常量 0——结果无论选哪一项都只会回填
       第一个扫到的网络。 */
    int idx = (int)lv_dropdown_get_selected(lv_event_get_target(e));
    if (idx < 0 || idx >= s_scan_n) return;
    lv_textarea_set_text(s_ta_ssid, s_scanned[idx]);

    const cw_net_cfg_t *cfg = net_store_get();
    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        if (cfg->profiles[i].used && strcmp(cfg->profiles[i].ssid, s_scanned[idx]) == 0) {
            lv_textarea_set_text(s_ta_pass, cfg->profiles[i].password);
            return;
        }
    }
}

/* 扫描完成后回到 LVGL 线程填下拉框。 */
static void scan_done_ui(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    if (!s_dd || gen != s_scan_gen) return;   /* 页面已换，丢弃这次结果 */

    if (s_scan_n <= 0) {
        lv_dropdown_set_options(s_dd, "(未扫到)");
        return;
    }
    char opts[SCAN_MAX * 34] = "";
    for (int i = 0; i < s_scan_n; i++) {
        strncat(opts, s_scanned[i], sizeof opts - strlen(opts) - 2);
        if (i < s_scan_n - 1) strncat(opts, "\n", sizeof opts - strlen(opts) - 1);
    }
    lv_dropdown_set_options(s_dd, opts);
}

/*
 * 扫描任务。wifi_net_scan() 要开射频并阻塞数秒，绝不能在 LVGL 线程里调
 * ——那会让整个界面卡死几秒（wifi_net.h 里就是这么写的，之前的实现却正是
 * 在 build_edit() 里直接调的）。
 */
static void scan_task(void *arg)
{
    uint32_t gen = (uint32_t)(uintptr_t)arg;
    int n = wifi_net_scan(s_scanned, SCAN_MAX);
    if (gen == s_scan_gen) {
        s_scan_n = n;
        ui_async_post(scan_done_ui, (void *)(uintptr_t)gen);
    }
    vTaskDelete(NULL);
}

static void back_to_list(lv_event_t *e) { (void)e; show(build_list); }

static void edit_on_delete(lv_event_t *e)
{
    (void)e;
    s_scan_gen++;          /* 在途的扫描结果作废 */
    s_scr = NULL; s_ta_ssid = s_ta_pass = s_kb = s_dd = NULL;
}

static lv_obj_t *build_edit(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, edit_on_delete, LV_EVENT_DELETE, NULL);

    char title[24];
    snprintf(title, sizeof title, "网络 %d", s_edit_idx + 1);
    lv_obj_t *bar = ui_topbar(s_scr, title, back_to_list);
    lv_obj_t *sv = ui_button(bar, "保存", save_cb, NULL);
    lv_obj_set_size(sv, 56, 24);
    lv_obj_align(sv, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_t *cl = ui_button(bar, "清空", clear_cb, NULL);
    lv_obj_set_size(cl, 56, 24);
    lv_obj_align(cl, LV_ALIGN_RIGHT_MID, -64, 0);

    const cw_net_cfg_t *cfg = net_store_get();

    /* 纵向预算（共 240）：顶栏 28 / SSID 34 / 密码 34 / 键盘 134，行间留 3 */
    const int y_ssid = UI_TOPBAR_H + 3;              /* 31 */
    const int y_pass = y_ssid + FIELD_H + 4;         /* 69 */
    const int y_kb   = y_pass + FIELD_H + 3;         /* 106 */

    s_ta_ssid = lv_textarea_create(s_scr);
    lv_textarea_set_one_line(s_ta_ssid, true);
    field_style(s_ta_ssid);
    /* 右侧留给扫描下拉框。之前两者都放在 (4,30) 且输入框是全宽的，
       下拉框直接盖住了它的左半边。 */
    lv_obj_set_pos(s_ta_ssid, 4, y_ssid);
    lv_obj_set_size(s_ta_ssid, 204, FIELD_H);
    lv_textarea_set_placeholder_text(s_ta_ssid, "SSID");
    lv_textarea_set_text(s_ta_ssid, cfg->profiles[s_edit_idx].ssid);
    lv_obj_add_event_cb(s_ta_ssid, ta_focus, LV_EVENT_FOCUSED, NULL);

    s_ta_pass = lv_textarea_create(s_scr);
    lv_textarea_set_one_line(s_ta_pass, true);
    field_style(s_ta_pass);
    lv_obj_set_pos(s_ta_pass, 4, y_pass);
    lv_obj_set_size(s_ta_pass, 312, FIELD_H);
    lv_textarea_set_placeholder_text(s_ta_pass, "密码");
    lv_textarea_set_text(s_ta_pass, cfg->profiles[s_edit_idx].password);
    lv_obj_add_event_cb(s_ta_pass, ta_focus, LV_EVENT_FOCUSED, NULL);

    /* 扫描结果作为 SSID 的快捷输入。先摆出下拉框显示「扫描中...」，
       真正的扫描扔给后台任务，结果回来再填——不阻塞界面。 */
    s_dd = lv_dropdown_create(s_scr);
    field_style(s_dd);
    lv_dropdown_set_options(s_dd, "扫描中...");
    lv_obj_set_pos(s_dd, 212, y_ssid);
    lv_obj_set_size(s_dd, 104, FIELD_H);
    lv_obj_add_event_cb(s_dd, pick_ssid, LV_EVENT_VALUE_CHANGED, NULL);

    /* 键盘。横屏下 textarea 必须在它上方，否则输入时看不见自己在打什么。
     *
     * 这里必须用 lv_obj_align(TOP_LEFT) 而不是 lv_obj_set_pos()：
     * lv_keyboard 的构造函数里有一句 lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0)，
     * 而 LVGL 8 中 align 一旦不是默认值，set_pos() 设的 x/y 就变成**相对该对齐
     * 点的偏移**。于是 set_pos(0, 106) 的实际效果是"在底部对齐的基础上再往下
     * 推 106px"——键盘被顶出屏幕，只露出顶部一条，上方还留下一大片空白。 */
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_align(s_kb, LV_ALIGN_TOP_LEFT, 0, y_kb);
    lv_obj_set_size(s_kb, 320, 240 - y_kb);
    /* 4 行按键要塞进 134px：把键盘自身和按键的 padding 压到最小，
       每行才有 (134-4-3*2)/4 = 31 > 行高 26 的余量。 */
    lv_obj_set_style_pad_all(s_kb, 2, 0);
    lv_obj_set_style_pad_row(s_kb, 2, 0);
    lv_obj_set_style_pad_column(s_kb, 2, 0);
    lv_obj_set_style_radius(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_kb, 0, LV_PART_ITEMS);
    /* 与 ui_common.c 的 make_btn 同理：默认主题给按下态加了放大 + 缓动，
       本项目禁止动画，显式清零。 */
    lv_obj_set_style_transform_width(s_kb, 0, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(s_kb, 0, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_keyboard_set_textarea(s_kb, s_ta_ssid);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    s_scan_n = 0;
    uint32_t gen = ++s_scan_gen;
    if (xTaskCreatePinnedToCore(scan_task, "wifi_scan", 4096,
                                (void *)(uintptr_t)gen, 5, NULL, 0) != pdPASS) {
        lv_dropdown_set_options(s_dd, "(扫描失败)");
    }
    return s_scr;
}

/* ---------------------------------------------------------------- 列表态 */

static void slot_cb(lv_event_t *e)
{
    s_edit_idx = (int)(intptr_t)lv_event_get_user_data(e);
    show(build_edit);
}

static void import_cb(lv_event_t *e)
{
    (void)e;
    int n = net_store_import_from_fmo();
    if (n > 0) show(build_list);
}

static void back_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }

static void list_on_delete(lv_event_t *e) { (void)e; s_scr = NULL; s_list = NULL; }

static lv_obj_t *build_list(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, list_on_delete, LV_EVENT_DELETE, NULL);
    ui_topbar(s_scr, "WiFi 配网", back_home);

    const cw_net_cfg_t *cfg = net_store_get();
    int y = UI_TOPBAR_H;
    int row_h = 42;

    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        char txt[64];
        if (cfg->profiles[i].used && cfg->profiles[i].ssid[0])
            snprintf(txt, sizeof txt, "%d  %s", i + 1, cfg->profiles[i].ssid);
        else
            snprintf(txt, sizeof txt, "%d  (空)", i + 1);
        lv_obj_t *cell = ui_cell(s_scr, txt, UI_FONT_CN, slot_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(cell, 0, y);
        lv_obj_set_size(cell, 320, row_h);
        y += row_h;
    }

    /* 只在本机一个都没配、而 fmo-companion 那边确实有的时候才提供导入。
       两个条件都要满足——否则这个按钮对已经配好的用户就是个误触陷阱。 */
    if (!net_store_has_wifi() && net_store_fmo_available()) {
        lv_obj_t *b = ui_button(s_scr, "从 FMO 伴侣导入", import_cb, NULL);
        lv_obj_set_size(b, 200, 30);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -6);
    }

    s_list = s_scr;
    return s_scr;
}

lv_obj_t *ui_wifi_create(void)
{
    /* 扫描和连接都要射频。进这一页就开着，退出时由对练页或主页负责关。 */
    wifi_net_init();
    wifi_net_start(NULL, NULL);
    return build_list();
}
