#include "ui_settings.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "esp_app_desc.h"
#include "cw_settings.h"
#include "cw_player.h"
#include "tone_gen.h"
#include "backlight.h"
#include "morse_timing.h"
#include "koch_engine.h"

/* 版本号不再硬编码：info 为 NULL 的 IT_INFO 行显示固件真正嵌入的版本
 * （来自 CMakeLists 的 PROJECT_VER），显示值与固件值不可能对不上。 */

typedef enum { IT_NUM, IT_CHOICE, IT_TOGGLE, IT_CALLSIGN, IT_RESET, IT_INFO } item_type_t;
typedef struct {
    const char *name; item_type_t type;
    int min, max, step; const char *unit;
    const char *const *choices; int nchoices;
    bool listen;   /* 数值页显示"试听" */
    const char *info; /* 仅 IT_INFO 用：这一行要显示的固定文本（原来写死 APP_VERSION，
                          现在两个 IT_INFO 行——作者/版本——各自带自己的值） */
} item_t;

static const char *const KEY_INPUTS[] = { "触摸屏", "外接口" };
static const char *const KEYER_MODES[] = { "直键", "Iambic A", "Iambic B" };
/* 写"上下翻转"而不是"180°"：`°`(U+00B0) 不在 font20 的 cmap 里（`fonts/font20.c`
 * 的字形表里没有 `unicode = 0x00b0` 这一项），会静默渲染成空白——正是 README
 * 「界面里的中文字体」一节描述的那类只有对着真屏幕才看得出来的坑。 */
static const char *const SCREEN_ROTS[] = { "正常", "上下翻转" };

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
    { "电键输入", IT_CHOICE, 0, 0, 0, "", KEY_INPUTS, 2, false, NULL },
    { "电键类型", IT_CHOICE, 0, 0, 0, "", KEYER_MODES, 3, false, NULL },
    { "桨键左右对调", IT_TOGGLE, 0, 0, 0, "", NULL, 0, false, NULL },
};
static const item_t GEN[] = {
    { "我的呼号", IT_CALLSIGN, 0, 0, 0, "", NULL, 0, false, NULL },
    { "Koch 课次", IT_NUM, 1, 39, 1, "", NULL, 0, false, NULL },
    { "屏幕亮度", IT_NUM, 10, 100, 10, "%", NULL, 0, false, NULL },
    { "屏幕旋转", IT_CHOICE, 0, 0, 0, "", SCREEN_ROTS, 2, false, NULL },
    { "恢复出厂设置", IT_RESET, 0, 0, 0, "", NULL, 0, false, NULL },
    { "作者", IT_INFO, 0, 0, 0, "", NULL, 0, false, "BI8SYN" },
    { "版本", IT_INFO, 0, 0, 0, "", NULL, 0, false, NULL },
};
static const item_t *const GROUPS[3] = { AUDIO, KEY, GEN };
static const int GROUP_N[3] = { 4, 3, 7 };
static const char *const GROUP_NAMES[3] = { "音频", "电键", "常规" };

static int s_group = 0;
static const item_t *s_item;
static lv_obj_t *s_list, *s_tabs[3];
static lv_obj_t *s_val_label;
static char s_call_buf[12];
static lv_obj_t *s_call_hint;

/* ---- 读写设置字段 ----
 * it->name 在这里被当成字段名做 strcmp 分派：新增/改名条目时必须同步这里
 * 和上面三张表，否则不会报编译错误，只会静默返回 0 / 什么都不写。 */
static int get_val(const item_t *it)
{
    cw_settings_t *c = cw_settings_get();
    if (!strcmp(it->name, "字符速度")) return c->char_wpm;
    if (!strcmp(it->name, "有效速度")) return c->eff_wpm;
    if (!strcmp(it->name, "音调")) return c->tone_hz;
    if (!strcmp(it->name, "音量")) return c->volume;
    if (!strcmp(it->name, "电键输入")) return c->key_input;
    if (!strcmp(it->name, "电键类型")) return c->keyer_mode;
    if (!strcmp(it->name, "桨键左右对调")) return c->paddle_swap;
    if (!strcmp(it->name, "Koch 课次")) return c->koch_lesson;
    if (!strcmp(it->name, "屏幕亮度")) return c->brightness;
    if (!strcmp(it->name, "屏幕旋转")) return c->rotate_180;
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
    cw_settings_t *c = cw_settings_get();
    if (!strcmp(it->name, "字符速度")) { c->char_wpm = (uint8_t)v; if (c->eff_wpm > c->char_wpm) c->eff_wpm = c->char_wpm; }
    else if (!strcmp(it->name, "有效速度")) { if (v > c->char_wpm) v = c->char_wpm; c->eff_wpm = (uint8_t)v; }
    else if (!strcmp(it->name, "音调")) c->tone_hz = (uint16_t)v;
    else if (!strcmp(it->name, "音量")) c->volume = (uint8_t)v;
    else if (!strcmp(it->name, "电键输入")) c->key_input = (uint8_t)v;
    else if (!strcmp(it->name, "电键类型")) c->keyer_mode = (uint8_t)v;
    else if (!strcmp(it->name, "桨键左右对调")) c->paddle_swap = (uint8_t)v;
    else if (!strcmp(it->name, "Koch 课次")) c->koch_lesson = (uint8_t)v;
    else if (!strcmp(it->name, "屏幕亮度")) { c->brightness = (uint8_t)v; backlight_set(c->brightness); }
    else if (!strcmp(it->name, "屏幕旋转")) { c->rotate_180 = (uint8_t)(v ? 1 : 0); apply_rotation(c->rotate_180); }
    apply_audio();
    cw_settings_save();
}

static void fmt_val(const item_t *it, char *out, size_t n)
{
    cw_settings_t *c = cw_settings_get();
    switch (it->type) {
    case IT_NUM: snprintf(out, n, "%d%s", get_val(it), it->unit); break;
    case IT_CHOICE: snprintf(out, n, "%s", it->choices[get_val(it)]); break;
    case IT_TOGGLE: snprintf(out, n, "%s", get_val(it) ? "开" : "关"); break;
    case IT_CALLSIGN: snprintf(out, n, "%s", c->callsign); break;
    case IT_INFO: snprintf(out, n, "%s", it->info ? it->info
                                              : esp_app_get_description()->version); break;
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
}

/* 数值调整页里的"试听"用 cw_player 全局单例播放 PARIS。&s_adj_scr 取本
 * 模块静态变量的地址当会话归属令牌——地址稳定、这一类页面共用同一个令牌，
 * 足够区分"是不是这个调整页自己发起的播放"。页面删除时用
 * cw_player_stop_if() 而不是无条件 cw_player_stop()：ui_nav_show 先建好
 * 新页面（可能已经开始播放）再异步删除旧页面，无条件停止会掐掉新页面刚
 * 起播的音频，而不是本页自己那一段——ui_preview/ui_practice 已经踩过这个
 * 坑，这里沿用同一套令牌机制。 */
static lv_obj_t *s_adj_scr;
static void adj_on_delete(lv_event_t *e) { (void)e; cw_player_stop_if(&s_adj_scr); s_adj_scr = NULL; }
static void listen_cb(lv_event_t *e) { (void)e; cw_player_play("PARIS", NULL, &s_adj_scr); }

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

/* 呼号编辑：4 行 × 9 列 + 退格/确定 */
static lv_obj_t *s_call_label;
static void call_key_cb(lv_event_t *e)
{
    const char *k = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    size_t l = strlen(s_call_buf);
    if (!strcmp(k, "退格")) {
        if (l) s_call_buf[l - 1] = 0;
        lv_label_set_text(s_call_hint, "");
    } else if (!strcmp(k, "确定")) {
        /* 长度不足时不能悄悄放弃：停在编辑页、给出提示，不导航离开——
         * 否则操作者看到页面正常返回，会以为已经保存成功。 */
        if (l < 3) { lv_label_set_text(s_call_hint, "呼号至少 3 个字符，未保存"); return; }
        strcpy(cw_settings_get()->callsign, s_call_buf);
        cw_settings_save();
        ui_nav_show(ui_settings_create);
        return;
    } else if (l < 10) {
        s_call_buf[l] = k[0]; s_call_buf[l + 1] = 0;
        lv_label_set_text(s_call_hint, "");
    }
    lv_label_set_text(s_call_label, s_call_buf);
}
static lv_obj_t *build_callsign(void)
{
    lv_obj_t *s = ui_screen_create();
    ui_topbar(s, "我的呼号", back_to_settings);
    strcpy(s_call_buf, cw_settings_get()->callsign);
    s_call_label = lv_label_create(s);
    lv_obj_set_style_text_font(s_call_label, UI_FONT_MID, 0);
    lv_label_set_text(s_call_label, s_call_buf);
    lv_obj_align(s_call_label, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + 2);
    s_call_hint = lv_label_create(s);
    lv_obj_set_style_text_font(s_call_hint, UI_FONT_CN, 0);
    lv_label_set_text(s_call_hint, "");
    lv_obj_align(s_call_hint, LV_ALIGN_TOP_MID, 0, UI_TOPBAR_H + 22);
    const char *keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int cols = 10, cw = 32, ch = 42, y0 = UI_TOPBAR_H + 40;
    for (int i = 0; i < 36; i++) {
        char t[2] = { keys[i], 0 };
        lv_obj_t *k = ui_cell(s, t, UI_FONT_CN, call_key_cb, NULL);
        lv_obj_set_pos(k, (i % cols) * cw, y0 + (i / cols) * ch); lv_obj_set_size(k, cw, ch);
    }
    lv_obj_t *bs = ui_cell(s, "退格", UI_FONT_CN, call_key_cb, NULL);
    lv_obj_set_pos(bs, 6 * cw, y0 + 3 * ch); lv_obj_set_size(bs, 2 * cw, ch);
    lv_obj_t *ok = ui_cell(s, "确定", UI_FONT_CN, call_key_cb, NULL);
    lv_obj_set_pos(ok, 8 * cw, y0 + 3 * ch); lv_obj_set_size(ok, 2 * cw, ch);
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
    case IT_CALLSIGN: ui_nav_show(build_callsign); break;
    case IT_RESET: ui_nav_show(build_reset); break;
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
    for (int g = 0; g < 3; g++) lv_obj_set_style_bg_color(s_tabs[g], g == s_group ? UI_GRAY_DOWN : UI_WHITE, 0);
}

static void tab_cb(lv_event_t *e) { s_group = (int)(intptr_t)lv_event_get_user_data(e); fill_list(); }
static void back_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }

lv_obj_t *ui_settings_create(void)
{
    lv_obj_t *scr = ui_screen_create();
    ui_topbar(scr, "设置", back_home);
    int h = (240 - UI_TOPBAR_H) / 3;
    for (int g = 0; g < 3; g++) {
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
