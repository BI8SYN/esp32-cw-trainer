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
 * SSID **只能从扫描结果里选**，不提供手工输入：手工敲 SSID 既费劲又容易
 * 敲错（错了还只能在连接失败时才发现），而能连上的网络必然扫得到。唯一的
 * 例外是槽位里已存的 SSID 当前不在范围内——那一条会被塞进列表首位，免得
 * 编辑一次就把它弄丢。
 *
 * 横屏 320x240 下键盘占掉下半屏，所以编辑态是独立一屏（不是弹层），
 * 密码框放在键盘上方——fmo-companion 那边是竖屏布局，不能照搬。
 */

#define SCAN_MAX 20

/*
 * 输入类控件的统一高度。UI_FONT_CN 行高 26，加上下 padding 3+3 和 border 1+1
 * 正好 34——项目里 ui_button() 用 36 是同一笔账。
 *
 * 这个数不能拍脑袋往小了写：对象比内容行高矮时 LVGL 会判定为溢出，既把文字
 * 往下偏移又给控件打开滚动，看起来就是"输入框只有一半高度"。ui_common.c 里
 * 顶栏返回箭头的注释记的是同一个坑。
 */
#define FIELD_H 34

/*
 * 自定义键盘映射。
 *
 * LVGL 默认的键盘映射用 LV_SYMBOL_BACKSPACE / NEW_LINE / KEYBOARD / LEFT /
 * RIGHT / OK 这六个图标键，它们的码点落在 0xF000+ 的私有区，而本项目的
 * tools/font-src/font20.c 里**一个都没有**（实测 grep "unicode = 0xF" 返回 0 条），
 * 于是全部渲染成豆腐块。
 *
 * 给字体补上那段字形要重新生成字体文件、还会增大固件体积，而这几个键用
 * ASCII 文字标注一样清楚，所以直接换映射。模式切换键 "abc"/"ABC"/"1#" 本来
 * 就是 ASCII，不用动。
 *
 * 换了文字就不能再用 lv_keyboard_def_event_cb —— 它是靠 strcmp 按键文字来
 * 分发的，认不出 "DEL" 会走到最后的 else 把 "DEL" 三个字母原样插进输入框。
 * 因此下面把默认回调摘掉，换成 kb_event()。
 */
static const char *kb_map_lc[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "DEL", "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "OK", "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    "<-", " ", "->", ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_lc[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 3,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 12, LV_KEYBOARD_CTRL_BTN_FLAGS | 3
};

static const char *kb_map_uc[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "DEL", "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "OK", "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    "<-", " ", "->", ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_uc[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 3,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 12, LV_KEYBOARD_CTRL_BTN_FLAGS | 3
};

/*
 * 符号页。目标是**可打印 ASCII 全覆盖**（0x20~0x7E）——WPA2 密码允许其中
 * 任意字符，少一个就有密码敲不进来。字母页那三行已经带了 _ - . , : 和空格，
 * 所以这一页负责剩下的 10 个数字 + 27 个符号。
 *
 * 初版漏了 ' " ^ ` | ~ 六个（LVGL 自带的默认符号页也漏了后四个），
 * 下面的 kb_coverage_check() 就是为了别再漏。
 *
 * 光标键用 "<-" / "->" 两个字符而不是 "<" / ">"，好把后者留给密码本身。
 */
static const char *kb_map_sp[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "DEL", "\n",
    "abc", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "[", "]", "{", "}", "\\", "|", ";", "'", "\"", "+", "OK", "\n",
    "<-", "/", "<", "=", ">", "?", "`", "~", " ", "->", ""
};
static const lv_btnmatrix_ctrl_t kb_ctrl_sp[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 3,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 3,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 2, 2, 2, 2, 2, 2, 2, 6, LV_KEYBOARD_CTRL_BTN_FLAGS | 3
};

/*
 * 映射表和 ctrl 表的长度必须严格对应：ctrl 表每个按键一项（不含 "\n" 与
 * 结尾的 ""）。少一项 LVGL 就会越界读 ctrl 表。手工数很容易数错，钉死在
 * 编译期。
 *   lc/uc: 12 + 11 + 12 + 3 = 38 个键，map 数组 38 + 3 个 "\n" + 1 个 "" = 42
 *   sp   : 11 + 11 + 11 + 10 = 43 个键，map 数组 43 + 3 + 1 = 47
 */
#define ARRLEN(a) (sizeof (a) / sizeof (a)[0])
_Static_assert(ARRLEN(kb_map_lc)  == 42, "kb_map_lc 键数与注释不符");
_Static_assert(ARRLEN(kb_ctrl_lc) == 38, "kb_ctrl_lc 必须每键一项");
_Static_assert(ARRLEN(kb_map_uc)  == 42, "kb_map_uc 键数与注释不符");
_Static_assert(ARRLEN(kb_ctrl_uc) == 38, "kb_ctrl_uc 必须每键一项");
_Static_assert(ARRLEN(kb_map_sp)  == 47, "kb_map_sp 键数与注释不符");
_Static_assert(ARRLEN(kb_ctrl_sp) == 43, "kb_ctrl_sp 必须每键一项");

static lv_obj_t *s_scr;
static lv_obj_t *s_ta_pass, *s_kb, *s_dd;
static int       s_edit_idx = -1;
static char      s_scanned[SCAN_MAX][33];
static int       s_scan_n;
static char      s_sel_ssid[33];     /* 当前选中的 SSID，保存时用它 */
static char      s_keep_ssid[33];    /* 槽位里已存、但当前可能扫不到的那个 */
/* 扫描是异步的，页面可能在扫描返回前就被删掉了。用代次而不是只判空指针：
   连续两次进出编辑页时，第一次的扫描任务可能在第二次建好页之后才回来，
   那时指针非空但已经是另一个页面了，直接往里塞结果就是张冠李戴。 */
static volatile uint32_t s_scan_gen;

static lv_obj_t *build_list(void);
static void show(lv_obj_t *(*builder)(void)) { ui_nav_show(builder); }

static void field_style(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_top(o, 3, 0);
    lv_obj_set_style_pad_bottom(o, 3, 0);
    lv_obj_set_style_pad_left(o, 6, 0);
    lv_obj_set_style_pad_right(o, 6, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, UI_INK, 0);
    lv_obj_set_style_bg_color(o, UI_RAISE, 0);
}

/* ---------------------------------------------------------------- 键盘 */

static void kb_event(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(kb);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char *txt = lv_btnmatrix_get_btn_text(kb, id);
    if (!txt) return;

    if (strcmp(txt, "abc") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER); return; }
    if (strcmp(txt, "ABC") == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER); return; }
    if (strcmp(txt, "1#")  == 0) { lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);    return; }

    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    if (!ta) return;

    if (strcmp(txt, "DEL") == 0) { lv_textarea_del_char(ta);     return; }
    if (strcmp(txt, "<-")  == 0) { lv_textarea_cursor_left(ta);  return; }
    if (strcmp(txt, "->")  == 0) { lv_textarea_cursor_right(ta); return; }
    if (strcmp(txt, "OK")  == 0) { lv_event_send(ta, LV_EVENT_READY, NULL); return; }

    lv_textarea_add_text(ta, txt);
}

/* ---------------------------------------------------------------- 编辑态 */

static void save_cb(lv_event_t *e)
{
    (void)e;
    if (s_edit_idx < 0) return;
    cw_net_cfg_t *cfg = net_store_get();
    cw_wifi_profile_t *p = &cfg->profiles[s_edit_idx];
    const char *pass = lv_textarea_get_text(s_ta_pass);

    memset(p, 0, sizeof *p);
    if (s_sel_ssid[0]) {
        snprintf(p->ssid, sizeof p->ssid, "%s", s_sel_ssid);
        snprintf(p->password, sizeof p->password, "%s", pass ? pass : "");
        p->used = 1;
    }
    /* 没选中任何 SSID = 清空该槽位，这是删除一个网络的唯一入口 */
    net_store_save();
    show(build_list);
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    s_sel_ssid[0] = '\0';
    lv_textarea_set_text(s_ta_pass, "");
}

/* 选中一个 SSID。若该 SSID 已存在于某个槽位，连密码一起回填——省掉重复
   输入，也让「改密码」变成「重选一次再改」。 */
static void pick_ssid(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char buf[64];
    lv_dropdown_get_selected_str(dd, buf, sizeof buf);
    /* 扫描还没回来时列表里只有占位文字，别把它当成 SSID 存下去 */
    if (buf[0] == '(' || strcmp(buf, "扫描中...") == 0) { s_sel_ssid[0] = '\0'; return; }
    snprintf(s_sel_ssid, sizeof s_sel_ssid, "%.32s", buf);

    const cw_net_cfg_t *cfg = net_store_get();
    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        if (cfg->profiles[i].used && strcmp(cfg->profiles[i].ssid, s_sel_ssid) == 0) {
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

    char opts[(SCAN_MAX + 1) * 34] = "";
    int sel = 0, n = 0;

    /* 槽位里已存但当前扫不到的 SSID 放首位，否则编辑一次就把它弄丢了 */
    bool keep_in_scan = false;
    for (int i = 0; i < s_scan_n; i++) {
        if (strcmp(s_scanned[i], s_keep_ssid) == 0) { keep_in_scan = true; break; }
    }
    if (s_keep_ssid[0] && !keep_in_scan) {
        strncat(opts, s_keep_ssid, sizeof opts - strlen(opts) - 2);
        n++;
    }

    for (int i = 0; i < s_scan_n; i++) {
        if (n) strncat(opts, "\n", sizeof opts - strlen(opts) - 1);
        strncat(opts, s_scanned[i], sizeof opts - strlen(opts) - 2);
        if (strcmp(s_scanned[i], s_sel_ssid) == 0) sel = n;
        n++;
    }

    if (n == 0) {
        lv_dropdown_set_options(s_dd, "(未扫到网络)");
        s_sel_ssid[0] = '\0';
        return;
    }
    lv_dropdown_set_options(s_dd, opts);
    lv_dropdown_set_selected(s_dd, sel);
    /* 让 s_sel_ssid 与列表实际选中项对齐 */
    char buf[64];
    lv_dropdown_get_selected_str(s_dd, buf, sizeof buf);
    snprintf(s_sel_ssid, sizeof s_sel_ssid, "%.32s", buf);
}

/*
 * 扫描任务。wifi_net_scan() 要开射频并阻塞数秒，绝不能在 LVGL 线程里调
 * ——那会让整个界面卡死几秒（wifi_net.h 里就是这么写的）。
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
    s_scr = NULL; s_ta_pass = s_kb = s_dd = NULL;
}

static lv_obj_t *build_edit(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, edit_on_delete, LV_EVENT_DELETE, NULL);

    char title[24];
    snprintf(title, sizeof title, "网络 %d", s_edit_idx + 1);
    lv_obj_t *bar = ui_topbar(s_scr, title, back_to_list);
/* 顶栏按钮统一用 ui_chip：原来是 ui_button 写死高 24，而 UI_FONT_CN 行高
       是 26——内容比容器高，LVGL 当溢出处理，文字下偏还会打开滚动。ui_chip
       的高度就等于行高，在 32px 顶栏里上下各留 2px。 */
    lv_obj_t *sv = ui_chip(bar, "保存", save_cb, NULL);
    lv_obj_align(sv, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_t *cl = ui_chip(bar, "清空", clear_cb, NULL);
    lv_obj_align(cl, LV_ALIGN_RIGHT_MID, -66, 0);

    const cw_net_cfg_t *cfg = net_store_get();
    snprintf(s_keep_ssid, sizeof s_keep_ssid, "%s", cfg->profiles[s_edit_idx].ssid);
    snprintf(s_sel_ssid, sizeof s_sel_ssid, "%s", cfg->profiles[s_edit_idx].ssid);

    /* 纵向预算（共 240）：顶栏 28 / SSID 34 / 密码 34 / 键盘 134，行间留 3 */
    const int y_ssid = UI_TOPBAR_H + 3;              /* 31 */
    const int y_pass = y_ssid + FIELD_H + 4;         /* 69 */
    const int y_kb   = y_pass + FIELD_H + 3;         /* 106 */

    /* SSID 只能从扫描结果里选，没有手工输入框。先摆出下拉框显示占位文字，
       真正的扫描扔给后台任务，结果回来再填——不阻塞界面。 */
    s_dd = lv_dropdown_create(s_scr);
    field_style(s_dd);
    /* 默认的下拉箭头是 LV_SYMBOL_DOWN，码点在 0xF000+ 私有区，本项目字体里
       没有这段字形，会渲染成豆腐块。这里干脆不显示符号。 */
    lv_dropdown_set_symbol(s_dd, NULL);
    lv_dropdown_set_options(s_dd, s_keep_ssid[0] ? s_keep_ssid : "扫描中...");
    lv_obj_set_pos(s_dd, 4, y_ssid);
    lv_obj_set_size(s_dd, 312, FIELD_H);
    lv_obj_add_event_cb(s_dd, pick_ssid, LV_EVENT_VALUE_CHANGED, NULL);

    s_ta_pass = lv_textarea_create(s_scr);
    lv_textarea_set_one_line(s_ta_pass, true);
    field_style(s_ta_pass);
    lv_obj_set_pos(s_ta_pass, 4, y_pass);
    lv_obj_set_size(s_ta_pass, 312, FIELD_H);
    lv_textarea_set_placeholder_text(s_ta_pass, "密码");
    lv_textarea_set_text(s_ta_pass, cfg->profiles[s_edit_idx].password);

    /* 键盘。
     *
     * 这里必须用 lv_obj_align(TOP_LEFT) 而不是 lv_obj_set_pos()：
     * lv_keyboard 的构造函数里有一句 lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0)，
     * 而 LVGL 8 中 align 一旦不是默认值，set_pos() 设的 x/y 就变成**相对该对齐
     * 点的偏移**。于是 set_pos(0, 106) 的实际效果是"在底部对齐的基础上再往下
     * 推 106px"——键盘被顶出屏幕，只露出顶部一条，上方还留下一大片空白。 */
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_align(s_kb, LV_ALIGN_TOP_LEFT, 0, y_kb);
    lv_obj_set_size(s_kb, 320, 240 - y_kb);

    /* 换掉带图标键的默认映射，并摘掉按文字 strcmp 分发的默认回调 */
    lv_obj_remove_event_cb(s_kb, lv_keyboard_def_event_cb);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lc, kb_ctrl_lc);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_uc, kb_ctrl_uc);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_SPECIAL,    kb_map_sp, kb_ctrl_sp);
    lv_obj_add_event_cb(s_kb, kb_event, LV_EVENT_VALUE_CHANGED, NULL);

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

    lv_keyboard_set_textarea(s_kb, s_ta_pass);
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

/*
 * 返回目标由调用方设定。配网页有三个入口——首次进对练页时自动跳转、
 * 设置里的「WiFi 网络」、对练页连不上时点状态带——退出时应该回到来的地方，
 * 而不是一律弹回主页（那样用户在设置里改完 WiFi 会莫名其妙被踢出设置）。
 */
static ui_screen_builder_t s_return_to;

static void back_home(lv_event_t *e)
{
    (void)e;
    ui_nav_show(s_return_to ? s_return_to : ui_home_create);
}

static void list_on_delete(lv_event_t *e) { (void)e; s_scr = NULL; }

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
    return s_scr;
}

void ui_wifi_set_return(ui_screen_builder_t builder) { s_return_to = builder; }

lv_obj_t *ui_wifi_create(void)
{
    /* 扫描和连接都要射频。进这一页就开着，退出时由对练页或主页负责关。 */
    wifi_net_init();
    wifi_net_start(NULL, NULL);
    return build_list();
}
