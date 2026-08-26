#include "ui_home.h"
#include <stdio.h>
#include <time.h>
#include "ui_common.h"
#include "ui_preview.h"
#include "ui_practice.h"
#include "ui_keyer.h"
#include "ui_settings.h"
#include "cw_settings.h"
#include "koch_engine.h"
#include "backlight.h"
#include "battery.h"

#define IDLE_OFF_MS 120000

/* 默认主题除了 grow（已在下面清零 transform_width/height）以外，还无条件给
   lv_btn_class 挂了两份 transition：transition_normal 挂在 selector=LV_STATE_PRESSED
   （按下瞬间渐变），transition_delayed 挂在 selector=0（松开后延迟渐变回默认态）。
   两份都会让本地按下态背景色（UI_GRAY_BG vs UI_WHITE）渐变而不是瞬时切换，仍是
   phase 1 禁止的动画。这里在同样的两个 selector 上用本地样式挂一份覆盖同一组
   属性、time=0 的描述符去顶掉它们（不是设成 NULL——那样会在 lv_obj_set_state
   内部被当成非空描述符解引用，直接崩溃；0 时长的合法描述符已用设备实测确认
   会被 lv_map 的边界分支直接返回终值，没有除零） */
static const lv_style_prop_t s_no_fade_props[] = {
    LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR,
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT,
    LV_STYLE_TRANSLATE_Y, LV_STYLE_TRANSLATE_X,
    LV_STYLE_TRANSFORM_ZOOM, LV_STYLE_TRANSFORM_ANGLE,
    LV_STYLE_COLOR_FILTER_OPA, LV_STYLE_COLOR_FILTER_DSC,
    0
};

static const lv_style_transition_dsc_t *no_fade_transition(void)
{
    static lv_style_transition_dsc_t dsc;
    static bool inited;
    if (!inited) {
        lv_style_transition_dsc_init(&dsc, s_no_fade_props, lv_anim_path_linear, 0, 0, NULL);
        inited = true;
    }
    return &dsc;
}

static lv_obj_t *s_scr, *s_clock, *s_bat;
static lv_timer_t *s_timer;
static bool s_dimmed;
static bool s_reset_notice_shown;

static void tick(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year > 100) lv_label_set_text_fmt(s_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else lv_label_set_text(s_clock, "--:--");

    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (!s_dimmed && idle > IDLE_OFF_MS) { backlight_set(0); s_dimmed = true; }
    else if (s_dimmed && idle < 1000) { backlight_set(cw_settings_get()->brightness); s_dimmed = false; }
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    lv_timer_del(s_timer);
    s_timer = NULL; s_scr = NULL;
    if (s_dimmed) { backlight_set(cw_settings_get()->brightness); s_dimmed = false; }
}

static void go_koch(lv_event_t *e)     { (void)e; ui_preview_open(cw_settings_get()->koch_lesson, false); }
static void go_call(lv_event_t *e)     { (void)e; ui_practice_open(TRAINER_MODE_CALLSIGN); }
static void go_qso(lv_event_t *e)      { (void)e; ui_practice_open(TRAINER_MODE_QSO); }
static void go_keyer(lv_event_t *e)    { (void)e; ui_nav_show(ui_keyer_create); }
static void go_settings(lv_event_t *e) { (void)e; ui_nav_show(ui_settings_create); }

lv_obj_t *ui_home_create(void)
{
    const cw_settings_t *cfg = cw_settings_get();
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = ui_topbar(s_scr, "", NULL);
    s_clock = lv_label_create(bar);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t *gear = lv_label_create(bar);
    lv_label_set_text(gear, "设置");
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_CLICKABLE);
    /* 电阻屏漏点比电容屏更常见；扩大到短边 >=48px（视觉文字大小不变，只是可点区域向外扩）。
       "设置" 只在主页出现，主页没有返回箭头，两者不会在同一页重叠 */
    lv_obj_set_ext_click_area(gear, 14);
    lv_obj_add_event_cb(gear, go_settings, LV_EVENT_CLICKED, NULL);

    s_bat = lv_label_create(bar);
    lv_obj_align(s_bat, LV_ALIGN_RIGHT_MID, -60, 0);
    ui_home_set_battery(battery_percent());

    int top = UI_TOPBAR_H;
    if (cw_settings_was_reset() && !s_reset_notice_shown) {
        ui_notice(s_scr, "进度已重置（数据损坏或版本不符）");
        s_reset_notice_shown = true;
        top += 22;
    }

    /* 左：Koch 卡片 */
    lv_obj_t *card = lv_btn_create(s_scr);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, UI_WHITE, 0);
    lv_obj_set_style_bg_color(card, UI_GRAY_BG, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(card, UI_BLACK, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    /* 默认主题给按下状态加了 transform_width/height 放大 + 80ms 缓动动画；
       phase 1 禁止任何动画，这里显式清零覆盖 */
    lv_obj_set_style_transform_width(card, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(card, 0, LV_STATE_PRESSED);
    /* 同一件事的另一半：顶掉主题按下/松开两份 transition，见文件顶部注释 */
    lv_obj_set_style_transition(card, no_fade_transition(), LV_STATE_PRESSED);
    lv_obj_set_style_transition(card, no_fade_transition(), 0);
    lv_obj_set_pos(card, 0, top);
    lv_obj_set_size(card, 200, 240 - top);
    lv_obj_add_event_cb(card, go_koch, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l1 = lv_label_create(card);
    /* 含中文（"第...课"），必须留在屏幕默认的 UI_FONT_CN，不能设成 UI_FONT_MID/UI_FONT_BIG（无中文字形） */
    lv_obj_set_style_text_color(l1, UI_BLACK, 0);
    lv_label_set_text_fmt(l1, "KOCH 第 %u 课", cfg->koch_lesson);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 0);

    char cs[48]; koch_charset(cfg->koch_lesson, cs);
    lv_obj_t *l2 = lv_label_create(card);
    lv_obj_set_style_text_color(l2, UI_BLACK, 0);
    lv_obj_set_width(l2, 184);
    lv_label_set_long_mode(l2, LV_LABEL_LONG_WRAP);
    if (cfg->koch_lesson == 1 && cfg->koch_last_pct[1] == 0xFF) lv_label_set_text_fmt(l2, "%s", cs);
    else lv_label_set_text_fmt(l2, "已解锁 %s", cs);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 40);

    lv_obj_t *l3 = lv_label_create(card);
    lv_obj_set_style_text_color(l3, UI_BLACK, 0);
    uint8_t last = cfg->koch_last_pct[cfg->koch_lesson];
    if (last == 0xFF) lv_label_set_text(l3, "未开始");
    else lv_label_set_text_fmt(l3, "上次 %u%%", last);
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 0, 96);

    lv_obj_t *l4 = lv_label_create(card);
    /* 含中文（"开始/继续练习"），必须留在屏幕默认的 UI_FONT_CN，不能设成 UI_FONT_MID/UI_FONT_BIG（无中文字形） */
    lv_obj_set_style_text_color(l4, UI_BLACK, 0);
    lv_label_set_text(l4, last == 0xFF ? "开始练习 >" : "继续练习 >");
    lv_obj_align(l4, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* 右：三个入口 */
    const char *names[3] = { "呼号抄收 >", "模拟 QSO >", "电键 >" };
    lv_event_cb_t cbs[3] = { go_call, go_qso, go_keyer };
    int h = (240 - top) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = ui_cell(s_scr, names[i], UI_FONT_CN, cbs[i], NULL);
        lv_obj_set_pos(c, 200, top + i * h);
        lv_obj_set_size(c, 120, h);
        lv_obj_set_style_border_side(c, i < 2 ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_NONE, 0);
    }

    s_dimmed = false;
    s_timer = lv_timer_create(tick, 1000, NULL);
    tick(s_timer);
    return s_scr;
}

void ui_home_set_battery(uint8_t percent)
{
    if (!s_scr || !s_bat) return;
    if (percent < 15) lv_label_set_text_fmt(s_bat, "电 %u%% !", percent);
    else lv_label_set_text_fmt(s_bat, "电 %u%%", percent);
}
