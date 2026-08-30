#include "ui_settings.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "esp_app_desc.h"
#include "cw_settings.h"
#include "net_store.h"
#include "stats_store.h"
#include "wifi_net.h"
#include "ui_ota.h"
#include "ui_wifi.h"
#include "ui_callsign.h"
#include "ota.h"
#include "cw_player.h"
#include "tone_gen.h"
#include "backlight.h"
#include "morse_timing.h"
#include "koch_engine.h"
#include "keyer.h"

/* 版本号不再硬编码：info 为 NULL 的 IT_INFO 行显示固件真正嵌入的版本
 * （来自 CMakeLists 的 PROJECT_VER），显示值与固件值不可能对不上。 */

typedef enum { IT_NUM, IT_CHOICE, IT_TOGGLE, IT_CALLSIGN, IT_RESET, IT_INFO, IT_OTA, IT_WIFI } item_type_t;
typedef struct {
    const char *name; item_type_t type;
    int min, max, step; const char *unit;
    const char *const *choices; int nchoices;
    bool listen;   /* 数值页显示"试听" */
    const char *info; /* 仅 IT_INFO 用：这一行要显示的固定文本（原来写死 APP_VERSION，
                          现在两个 IT_INFO 行——作者/版本——各自带自己的值） */
} item_t;

static const char *const KEYER_MODES[] = { "直键", "Iambic A", "Iambic B" };
/* 写"上下翻转"而不是"180°"：`°`(U+00B0) 不在 font20 的 cmap 里（`fonts/font20.c`
 * 的字形表里没有 `unicode = 0x00b0` 这一项），会静默渲染成空白——正是 README
 * 「界面里的中文字体」一节描述的那类只有对着真屏幕才看得出来的坑。 */
static const char *const SCREEN_ROTS[] = { "正常", "上下翻转" };
/* 顺序必须与 cw_settings.h 的 CW_BATT_HIDE/ICON/NUM 一致——
   IT_CHOICE 存的就是下标本身。 */
/* 「图标+数字」用加号而不是「和」：fmt_val 的调用处缓冲是 char b[16]，
   五个中文正好 15 字节 + NUL = 16，零余量；以后谁多加一个字就会在
   UTF-8 字符中间截断，屏幕上出现半个乱码字。加号版 13 字节，留得住。 */
static const char *const BATT_SHOWS[] = { "隐藏", "图标", "图标+数字" };
/*
 * 「连接网络」的选项在运行时按已保存的槽位生成：自动 + 每个非空槽位的
 * SSID。IT_CHOICE 的 choices 是 const char* 数组，所以这里备一份可写的
 * 缓冲，进设置页时填。
 */
#define NET_PICK_MAX (CW_WIFI_PROFILE_MAX + 1)
static char        s_net_pick_buf[NET_PICK_MAX][34];
static const char *s_net_pick[NET_PICK_MAX];
static int         s_net_pick_n = 1;

static void rebuild_net_picks(void)
{
    const cw_net_cfg_t *nc = net_store_get();
    snprintf(s_net_pick_buf[0], sizeof s_net_pick_buf[0], "自动（最强）");
    s_net_pick[0] = s_net_pick_buf[0];
    int n = 1;
    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        if (!nc->profiles[i].used || !nc->profiles[i].ssid[0]) continue;
        snprintf(s_net_pick_buf[n], sizeof s_net_pick_buf[n], "%s", nc->profiles[i].ssid);
        s_net_pick[n] = s_net_pick_buf[n];
        n++;
    }
    s_net_pick_n = n;
}

/* 下面三张表里的 name 字符串同时也是 get_val()/set_val() 的分派键（按
 * strcmp 匹配）。改这里的 name 必须同步改那两个函数里对应的字符串——两边
 * 不一致时不会有编译错误，只会静默地读到 0 或者什么都不写。ID 化分派的
 * 重构已记录为最终评审的待办项，这里先加注释把这条隐性耦合钉死。 */
static const item_t AUDIO[] = {
    { "字符速度", IT_NUM, 5, 40, 1, " WPM", NULL, 0, true, NULL },
    { "有效速度", IT_NUM, 5, 40, 1, " WPM", NULL, 0, true, NULL },
    { "音调",     IT_NUM, 400, 1000, 50, " Hz", NULL, 0, true, NULL },
    { "音量",     IT_NUM, 0, 100, 10, "%", NULL, 0, true, NULL },
};
static const item_t KEY[] = {
    { "电键类型", IT_CHOICE, 0, 0, 0, "", KEYER_MODES, 3, false, NULL },
    { "桨键左右对调", IT_TOGGLE, 0, 0, 0, "", NULL, 0, false, NULL },
};
static const item_t GEN[] = {
    { "我的呼号", IT_CALLSIGN, 0, 0, 0, "", NULL, 0, false, NULL },
    { "Koch 课次", IT_NUM, 1, 39, 1, "", NULL, 0, false, NULL },
    { "电量显示", IT_CHOICE, 0, 0, 0, "", BATT_SHOWS, 3, false, NULL },
    { "屏幕亮度", IT_NUM, 10, 100, 10, "%", NULL, 0, false, NULL },
    { "屏幕旋转", IT_CHOICE, 0, 0, 0, "", SCREEN_ROTS, 2, false, NULL },
    /*
     * 作者 / 版本 / 固件升级 是一个"我在跑哪个版本、怎么换一个"的小集群，
     * 用户想的是同一件事，不该按实现方式拆开（升级走 HTTP 不代表它属于
     * 网络设置）。iOS 设置→通用 就是「关于 / 软件更新 / 还原」挨在一起。
     *
     * 没有独立的「关于」页签：左侧竖排页签从 4 个变 5 个会让每个从 53px
     * 掉到 42px，低于 44px 触控下限——主页刚为同一条规则做过调整，设置页
     * 不能反着来。而且只有三行、其中两行还不可点，撑不起一个页签。
     */
    { "作者", IT_INFO, 0, 0, 0, "", NULL, 0, false, "BI8SYN" },
    { "版本", IT_INFO, 0, 0, 0, "", NULL, 0, false, NULL },
    { "固件升级", IT_OTA, 0, 0, 0, "", NULL, 0, false, NULL },
    { "开机检查更新", IT_TOGGLE, 0, 0, 0, "", NULL, 0, false, NULL },
    /* 危险操作按惯例压在最末 */
    { "恢复出厂设置", IT_RESET, 0, 0, 0, "", NULL, 0, false, NULL },
};
/*
 * 网络组的字段存在 net_store（独立 NVS key），不在 cw_settings_t 里——往
 * 后者加字段会清空 Koch 进度，见 net_store.h。所以 get_val/set_val 里这几
 * 条要走 net_store_get() 并调 net_store_save()。
 */
static item_t NET[] = {
    /*
     * 「WiFi 网络」是**添加/删除/改密码**的唯一入口。
     *
     * 在这之前配网页只在「首次进对练页且一个网络都没存」时才会自动跳出来，
     * 也就是说存过一个之后就再也进不去了——换个地方、存的网络都不在范围内，
     * 对练页只会显示"连不上"，然后就没有然后了。功能不可达是结构缺陷，
     * 优先级高于任何分组问题。
     *
     * 它和下面的「连接网络」不是一回事：这个管有哪些网络，那个管用哪一个。
     */
    { "WiFi 网络", IT_WIFI, 0, 0, 0, "", NULL, 0, false, NULL },
    { "连接网络", IT_CHOICE, 0, 0, 0, "", NULL, 0, false, NULL },
    { "收报缓冲", IT_NUM, 200, 1000, 100, " ms", NULL, 0, false, NULL },
    /*
     * 「对练服务器」那一行删了：只读、改不了，而连不上时对练页会直接显示
     * 「连不上服务器 <地址>」，设置页再摆一遍是纯噪音。
     * 「固件升级」移到了常规组，和版本挨着——见那边的说明。
     */
    { "wire 号",  IT_NUM, 1, 999, 1, "", NULL, 0, false, NULL },
};

static const item_t *const GROUPS[4] = { AUDIO, KEY, GEN, NET };
static const int GROUP_N[4] = { 4, 2, 10, 4 };
static const char *const GROUP_NAMES[4] = { "音频", "电键", "常规", "网络" };
#define GROUP_COUNT 4

static int s_group = 0;
static const item_t *s_item;
static lv_obj_t *s_list, *s_tabs[GROUP_COUNT];
static lv_obj_t *s_val_label;
static lv_obj_t *s_val_hint;   /* 数值下方的补充说明，目前只有 wire 号用 */
static void update_hint(void);

/* ---- 读写设置字段 ----
 * it->name 在这里被当成字段名做 strcmp 分派：新增/改名条目时必须同步这里
 * 和上面三张表，否则不会报编译错误，只会静默返回 0 / 什么都不写。 */
static int get_val(const item_t *it)
{
    cw_settings_t *c = cw_settings_get();
    cw_net_cfg_t *nc = net_store_get();
    if (!strcmp(it->name, "连接网络")) {
        /* wifi_net 里 -1 表示自动，这里 0 是"自动"、1.. 对应槽位顺序 */
        int p = wifi_net_preferred();
        if (p < 0) return 0;
        int n = 1;
        for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
            if (!nc->profiles[i].used || !nc->profiles[i].ssid[0]) continue;
            if (i == p) return n;
            n++;
        }
        return 0;
    }
    if (!strcmp(it->name, "开机检查更新")) return nc->boot_check ? 1 : 0;
    if (!strcmp(it->name, "收报缓冲")) return nc->rx_delay_ms;
    if (!strcmp(it->name, "wire 号")) return nc->wire;
    if (!strcmp(it->name, "字符速度")) return c->char_wpm;
    if (!strcmp(it->name, "有效速度")) return c->eff_wpm;
    if (!strcmp(it->name, "音调")) return c->tone_hz;
    if (!strcmp(it->name, "音量")) return c->volume;
    if (!strcmp(it->name, "电键类型")) return c->keyer_mode;
    if (!strcmp(it->name, "桨键左右对调")) return c->paddle_swap;
    if (!strcmp(it->name, "Koch 课次")) return c->koch_lesson;
    if (!strcmp(it->name, "屏幕亮度")) return c->brightness;
    if (!strcmp(it->name, "屏幕旋转")) return c->rotate_180;
    if (!strcmp(it->name, "电量显示")) return c->battery_show;
    return 0;
}

static void apply_audio(void)
{
    cw_settings_t *c = cw_settings_get();
    morse_timing_t tm; morse_timing_compute(c->char_wpm, c->eff_wpm, &tm);
    cw_player_set_timing(&tm);
    tone_gen_set_freq(c->tone_hz);
    tone_gen_set_volume(c->volume);
}

/* 立刻生效的屏幕翻转。
 *
 * 开机那一次走的是 main.c 里的 lv_port_disp_set_rotate_180()（必须在
 * lv_port_disp_init() 之前）；这里是运行时改设置时的那一次，只能走
 * lv_disp_set_rotation()——disp_drv 已经注册过了，再改那个静态标志没有用。
 *
 * 180° 不交换宽高，所有页面的坐标、尺寸都不用动；触摸坐标也不用自己翻，
 * LVGL 8 会按 disp_drv.rotated 变换（components/lvgl/src/core/lv_indev.c:347）。
 * 旋转本身由 lv_port_disp_init() 里打开的 sw_rotate 在 flush 前就地完成
 * （draw_buf_rotate_180），对局部刷新的双缓冲配置是安全的。 */
static void apply_rotation(uint8_t rotate_180)
{
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp) return;
    lv_disp_set_rotation(disp, rotate_180 ? LV_DISP_ROT_180 : LV_DISP_ROT_NONE);
}

/* 同 get_val()：it->name 是分派键，改名字必须两边一起改。 */
static void set_val(const item_t *it, int v)
{
    cw_net_cfg_t *nc = net_store_get();
    if (!strcmp(it->name, "连接网络")) {
        if (v <= 0) { wifi_net_prefer(-1); return; }
        int n = 1;
        for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
            if (!nc->profiles[i].used || !nc->profiles[i].ssid[0]) continue;
            if (n == v) { wifi_net_prefer(i); return; }
            n++;
        }
        wifi_net_prefer(-1);
        return;
    }
    if (!strcmp(it->name, "开机检查更新")) { nc->boot_check = (uint8_t)(v ? 1 : 0); net_store_save(); return; }
    if (!strcmp(it->name, "收报缓冲")) { nc->rx_delay_ms = (uint16_t)v; net_store_save(); return; }
    if (!strcmp(it->name, "wire 号")) { nc->wire = (int16_t)v; net_store_save(); return; }

    cw_settings_t *c = cw_settings_get();
    if (!strcmp(it->name, "字符速度")) { c->char_wpm = (uint8_t)v; if (c->eff_wpm > c->char_wpm) c->eff_wpm = c->char_wpm; }
    else if (!strcmp(it->name, "有效速度")) { if (v > c->char_wpm) v = c->char_wpm; c->eff_wpm = (uint8_t)v; }
    else if (!strcmp(it->name, "音调")) c->tone_hz = (uint16_t)v;
    else if (!strcmp(it->name, "音量")) c->volume = (uint8_t)v;
    else if (!strcmp(it->name, "电键类型")) c->keyer_mode = (uint8_t)v;
    else if (!strcmp(it->name, "桨键左右对调")) c->paddle_swap = (uint8_t)v;
    else if (!strcmp(it->name, "Koch 课次")) c->koch_lesson = (uint8_t)v;
    else if (!strcmp(it->name, "屏幕亮度")) { c->brightness = (uint8_t)v; backlight_set(c->brightness); }
    else if (!strcmp(it->name, "屏幕旋转")) { c->rotate_180 = (uint8_t)(v ? 1 : 0); apply_rotation(c->rotate_180); }
    /* 顶栏是在 ui_home_create() 里按这个值排的，改完要回主页才看得到——
       和「屏幕旋转」不同，那个能当场生效。这里不主动跳页：用户正在设置页
       连着调几项是常态，被踢回主页比晚看到一眼更烦。 */
    else if (!strcmp(it->name, "电量显示")) c->battery_show = (uint8_t)v;
    apply_audio();
    cw_settings_save();
}

static void fmt_val(const item_t *it, char *out, size_t n)
{
    cw_settings_t *c = cw_settings_get();
    switch (it->type) {
    case IT_NUM: snprintf(out, n, "%d%s", get_val(it), it->unit); break;
    case IT_CHOICE: {
        /* choices 为空或下标越界不该崩——这一整套是按 name 做 strcmp 分派的，
           表和分派函数不同步时本来就容易出错，不能让它以空指针解引用收场。 */
        int v = get_val(it);
        if (!it->choices || it->nchoices <= 0 || v < 0 || v >= it->nchoices)
            snprintf(out, n, "-");
        else
            snprintf(out, n, "%s", it->choices[v]);
        break;
    }
    case IT_TOGGLE: snprintf(out, n, "%s", get_val(it) ? "开" : "关"); break;
    /* 空呼号显示「未设置」——一片空白看起来像渲染坏了，而这一项现在出厂
       就是空的，用户得看出来它在等自己填。 */
    case IT_CALLSIGN: snprintf(out, n, "%s", c->callsign[0] ? c->callsign : "未设置"); break;
    case IT_OTA: {
        /* 显示"有没有新的"，不显示分区标签——ota_0/ota_1 是实现细节，
           用户既不关心也看不懂。 */
        const ota_check_t *ck = ota_check_result();
        if (ota_check_busy())   snprintf(out, n, "检查中");
        else if (!ck->checked)  snprintf(out, n, "未检查");
        else if (ck->err[0])    snprintf(out, n, "检查失败");
        else if (ck->available) snprintf(out, n, "有 %s", ck->latest);
        else                    snprintf(out, n, "已最新");
        break;
    }
    case IT_WIFI: {
        /* 值显示已存几个——是 0 的话，"联网对练连不上"的原因一眼可见 */
        const cw_net_cfg_t *nc = net_store_get();
        int k = 0;
        for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++)
            if (nc->profiles[i].used && nc->profiles[i].ssid[0]) k++;
        snprintf(out, n, "%d 个", k);
        break;
    }
    case IT_INFO:
        snprintf(out, n, "%s", it->info ? it->info
                                        : esp_app_get_description()->version);
        break;
    default: out[0] = 0;
    }
}

/* ---- 子页面 ---- */
static void back_to_settings(lv_event_t *e) { (void)e; ui_nav_show(ui_settings_create); }

static void adj_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    int v = get_val(s_item) + d * s_item->step;
    if (v < s_item->min) v = s_item->min;
    if (v > s_item->max) v = s_item->max;
    set_val(s_item, v);
    char b[16]; fmt_val(s_item, b, sizeof b);
    lv_label_set_text(s_val_label, b);
    update_hint();
}

/* 数值调整页里的"试听"用 cw_player 全局单例播放 PARIS。&s_adj_scr 取本
 * 模块静态变量的地址当会话归属令牌——地址稳定、这一类页面共用同一个令牌，
 * 足够区分"是不是这个调整页自己发起的播放"。页面删除时用
 * cw_player_stop_if() 而不是无条件 cw_player_stop()：ui_nav_show 先建好
 * 新页面（可能已经开始播放）再异步删除旧页面，无条件停止会掐掉新页面刚
 * 起播的音频，而不是本页自己那一段——ui_preview/ui_practice 已经踩过这个
 * 坑，这里沿用同一套令牌机制。 */
static lv_obj_t *s_adj_scr;
static void adj_on_delete(lv_event_t *e) { (void)e; cw_player_stop_if(&s_adj_scr); s_adj_scr = NULL; s_val_hint = NULL; }
static void listen_cb(lv_event_t *e) { (void)e; cw_player_play("PARIS", NULL, &s_adj_scr); }

/* 数值页下方的补充说明。 */
static void update_hint(void)
{
    if (!s_val_hint || !s_item) return;
    if (strcmp(s_item->name, "wire 号") == 0) {
        const char *n = cw_net_wire_label((int16_t)get_val(s_item));
        lv_label_set_text(s_val_hint, n ? n : "");
    } else if (strcmp(s_item->name, "电键类型") == 0) {
        /* A 和 B 的区别只在**双桨同时按下**（squeeze）时才显现，而电阻屏不支持
           多点触控——用触摸桨时选 A 还是 B 行为完全一样。不说的话，用户会以为
           自己选的没生效。只在选到 A/B 时说，直键跟这件事无关。 */
        lv_label_set_text(s_val_hint,
                          get_val(s_item) == KEYER_MODE_STRAIGHT
                          ? "" : "A/B 之别需双桨同按，仅外接电键可分");
    } else {
        lv_label_set_text(s_val_hint, "");
    }
}

static lv_obj_t *build_adjust(void)
{
    lv_obj_t *s = ui_screen_create();
    s_adj_scr = s;
    lv_obj_add_event_cb(s, adj_on_delete, LV_EVENT_DELETE, NULL);
    ui_topbar(s, s_item->name, back_to_settings);
    lv_obj_t *minus = ui_cell(s, "-", UI_FONT_BIG, adj_cb, (void *)(intptr_t)-1);
    lv_obj_set_size(minus, 64, 64); lv_obj_set_pos(minus, 24, 70);
    lv_obj_t *plus = ui_cell(s, "+", UI_FONT_BIG, adj_cb, (void *)(intptr_t)1);
    lv_obj_set_size(plus, 64, 64); lv_obj_set_pos(plus, 232, 70);
    s_val_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_val_label, UI_FONT_MID, 0);
    char b[16]; fmt_val(s_item, b, sizeof b);
    lv_label_set_text(s_val_label, b);
    lv_obj_align(s_val_label, LV_ALIGN_TOP_MID, 0, 88);

    /* wire 号下面显示这条 wire 上跑着哪个陪练。号码本身对用户没有意义，
       知道"上面是谁"才谈得上选。没有陪练的 wire 就不显示——留白比显示
       一句"无"更干净，也不会让人以为出了什么问题。 */
    s_val_hint = lv_label_create(s);
    lv_obj_set_style_text_font(s_val_hint, UI_FONT_CN, 0);
    /* 定宽 + 自动换行：「电键类型」那条说明一行放不下（font20 的中文字形约
       20px 宽，320px 只够 16 个字），不定宽的话 label 会按自然宽度撑出屏幕两侧。
       两行落在 132..184，下面的「试听」按钮在 170——但有试听的都是音频组的数值
       项，它们的 hint 是空的，不会撞上。 */
    lv_obj_set_width(s_val_hint, 300);
    lv_label_set_long_mode(s_val_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_val_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_val_hint, LV_ALIGN_TOP_MID, 0, 132);
    lv_label_set_text(s_val_hint, "");
    update_hint();
    if (s_item->listen) {
        lv_obj_t *l = ui_button(s, "试听", listen_cb, NULL);
        lv_obj_set_width(l, 120); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 170);
    }
    return s;
}

static void choice_cb(lv_event_t *e)
{
    set_val(s_item, (int)(intptr_t)lv_event_get_user_data(e));
    ui_nav_show(ui_settings_create);
}
static lv_obj_t *build_choice(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, s_item->name, back_to_settings);
    int cur = get_val(s_item);
    for (int i = 0; i < s_item->nchoices; i++) {
        char t[24]; snprintf(t, sizeof t, "%s %s", i == cur ? "*" : "  ", s_item->choices[i]);
        lv_obj_t *c = ui_cell(s, t, UI_FONT_CN, choice_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(c, 0, UI_TOPBAR_H + i * 52); lv_obj_set_size(c, 320, 52);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_BOTTOM, 0);
    }
    return s;
}

static void reset_yes(lv_event_t *e) { (void)e; cw_settings_reset(); apply_audio(); backlight_set(cw_settings_get()->brightness); apply_rotation(cw_settings_get()->rotate_180); ui_nav_show(ui_home_create); }
static lv_obj_t *build_reset(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "恢复出厂设置", back_to_settings);
    lv_obj_t *l = lv_label_create(s);
    lv_label_set_text(l, "将清除 Koch 进度和全部设置，不可恢复");
    lv_obj_set_width(l, 280); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_t *no = ui_button(s, "取消", back_to_settings, NULL); lv_obj_set_width(no, 120); lv_obj_set_pos(no, 30, 160);
    lv_obj_t *yes = ui_button(s, "确认清除", reset_yes, NULL); lv_obj_set_width(yes, 120); lv_obj_set_pos(yes, 170, 160);
    return s;
}

/* ---- 主设置页 ---- */

static void item_cb(lv_event_t *e)
{
    s_item = lv_event_get_user_data(e);
    switch (s_item->type) {
    case IT_NUM: ui_nav_show(build_adjust); break;
    case IT_CHOICE: ui_nav_show(build_choice); break;
    case IT_TOGGLE: set_val(s_item, !get_val(s_item)); ui_nav_show(ui_settings_create); break;
    /* 弹窗盖在设置页上，不换页。确定后重建设置页，让列表里那一行显示新呼号；
       取消传 NULL——关掉弹窗停在原地就是对的。 */
    case IT_CALLSIGN:
        ui_callsign_set_return(ui_settings_create);
        ui_nav_show(ui_callsign_create);
        break;
    case IT_RESET: ui_nav_show(build_reset); break;
    case IT_OTA: ui_nav_show(ui_ota_create); break;
    case IT_WIFI:
        /* 改完 WiFi 回到设置，而不是被弹回主页 */
        ui_wifi_set_return(ui_settings_create);
        ui_nav_show(ui_wifi_create);
        break;
    default: break;
    }
}

static void fill_list(void)
{
    lv_obj_clean(s_list);
    for (int i = 0; i < GROUP_N[s_group]; i++) {
        const item_t *it = &GROUPS[s_group][i];
        char v[24]; fmt_val(it, v, sizeof v);
        char t[48]; snprintf(t, sizeof t, "%s", it->name);
        lv_obj_t *row = ui_cell(s_list, t, UI_FONT_CN, it->type == IT_INFO ? NULL : item_cb, (void *)it);
        lv_obj_set_size(row, 260, 42);
        lv_obj_set_pos(row, 0, i * 42);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *vl = lv_label_create(row);
        if (it->type == IT_INFO) lv_label_set_text(vl, v);
        else lv_label_set_text_fmt(vl, "%s >", v);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    for (int g = 0; g < GROUP_COUNT; g++) lv_obj_set_style_bg_color(s_tabs[g], g == s_group ? UI_GRAY_DOWN : UI_WHITE, 0);
}

static void tab_cb(lv_event_t *e) { s_group = (int)(intptr_t)lv_event_get_user_data(e); fill_list(); }
static void back_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }

lv_obj_t *ui_settings_create(void)
{
    rebuild_net_picks();
    /*
     * 按名字找那一行，不要写死下标。
     *
     * 之前这里是 NET[0]，而后来在表头插了「WiFi 网络」——choices 就挂到了
     * 那个 IT_WIFI 行上，真正的「连接网络」拿到的是 NULL，fmt_val 里
     * it->choices[...] 直接空指针解引用，点一下网络页签就崩溃重启（表现
     * 为"跳回首页"）。下标会随插行静默错位，而且不会有编译错误。
     */
    for (int i = 0; i < GROUP_N[3]; i++) {
        if (strcmp(NET[i].name, "连接网络") == 0) {
            NET[i].choices = s_net_pick;
            NET[i].nchoices = s_net_pick_n;
            break;
        }
    }
    lv_obj_t *scr = ui_screen_create();
    ui_topbar(scr, "设置", back_home);
    int h = (240 - UI_TOPBAR_H) / GROUP_COUNT;
    for (int g = 0; g < GROUP_COUNT; g++) {
        s_tabs[g] = ui_cell(scr, GROUP_NAMES[g], UI_FONT_CN, tab_cb, (void *)(intptr_t)g);
        lv_obj_set_pos(s_tabs[g], 0, UI_TOPBAR_H + g * h); lv_obj_set_size(s_tabs[g], 60, h);
    }
    s_list = lv_obj_create(scr);
    lv_obj_set_pos(s_list, 60, UI_TOPBAR_H); lv_obj_set_size(s_list, 260, 240 - UI_TOPBAR_H);
    lv_obj_set_style_pad_all(s_list, 0, 0); lv_obj_set_style_radius(s_list, 0, 0);
    lv_obj_set_style_border_width(s_list, 1, 0); lv_obj_set_style_border_side(s_list, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(s_list, UI_BLACK, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    fill_list();
    return scr;
}
