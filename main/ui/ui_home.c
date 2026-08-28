#include "ui_home.h"
#include <stdio.h>
#include <time.h>
#include "ui_common.h"
#include "ui_preview.h"
#include "ui_practice.h"
#include "ui_keyer.h"
#include "ui_net.h"
#include "ui_stats.h"
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
static uint32_t s_last_idle;
static bool s_reset_notice_shown;

/* 灭屏期间显示和触摸层其实一直是活的：只是背光被设成 0，控件树、点击事件都
 * 照常工作。这意味着"把屏幕点亮的那一下"本身就会被当成一次普通点击送到
 * 底下的控件——Koch 卡片占了主页大半个屏幕，几乎每次唤醒都会因此顺手点开一课、
 * 出声。各入口的点击回调开头都先调用这个函数：如果当前确实是灭屏状态，就地
 * 点亮背光、清 s_dimmed，并返回 true 让调用方直接 return、不再执行正常的跳转/
 * 播放动作——这一下点击视为"仅用于唤醒"，被吃掉。s_dimmed 在函数内部就地清零，
 * 下一次点击进来时已经是正常状态，不会出现"点第一下没反应，之后怎么点都没
 * 反应"的卡死。 */
static bool wake_swallow(void)
{
    if (!s_dimmed) return false;
    backlight_set(cw_settings_get()->brightness);
    s_dimmed = false;
    return true;
}

static void tick(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year > 100) lv_label_set_text_fmt(s_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    else lv_label_set_text(s_clock, "--:--");

    uint32_t idle = lv_disp_get_inactive_time(NULL);
    if (!s_dimmed && idle > IDLE_OFF_MS) {
        backlight_set(0);
        s_dimmed = true;
    } else if (s_dimmed && idle < s_last_idle) {
        /* 原来是 "idle < 1000" 与这个每 1000ms 才跑一次的定时器比对：这个窗口
         * 假设了"上次检查到现在必然发生在 1000ms 之内"，但定时器本身就是每
         * 1000ms 才跑一次，一次点击如果落在两次检查之间比较靠后的位置，等到
         * 下一次 tick() 真正跑到时，idle 完全可能已经涨到 1000ms 上下甚至
         * 略微超过（主循环调度抖动、lv_timer_handler 处理耗时都会往上加），
         * 这一次点击就会被判定为"太旧"而不点亮屏幕，用户得再点一下才能碰到
         * 下一次窗口。这里改成"这一次读到的 idle 比上一次检查时还要小"——
         * 只要在两次 tick() 之间发生过任意一次输入，inactive_time 就会被
         * LVGL 重置变小，与两次检查之间实际间隔了多久、有没有抖动都无关，
         * 是比固定阈值更宽、更不依赖检查周期的判定。 */
        backlight_set(cw_settings_get()->brightness);
        s_dimmed = false;
    }
    s_last_idle = idle;
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    lv_timer_del(s_timer);
    s_timer = NULL; s_scr = NULL;
    if (s_dimmed) { backlight_set(cw_settings_get()->brightness); s_dimmed = false; }
}

static void go_koch(lv_event_t *e)     { (void)e; if (wake_swallow()) return; ui_preview_open(cw_settings_get()->koch_lesson, false); }
static void go_call(lv_event_t *e)     { (void)e; if (wake_swallow()) return; ui_practice_open(TRAINER_MODE_CALLSIGN); }
static void go_qso(lv_event_t *e)      { (void)e; if (wake_swallow()) return; ui_practice_open(TRAINER_MODE_QSO); }
static void go_keyer(lv_event_t *e)    { (void)e; if (wake_swallow()) return; ui_nav_show(ui_keyer_create); }
/* ui_net_create() 自己会在没配过网时改跳配网页，这里不用判断。 */
static void go_net(lv_event_t *e)      { (void)e; if (wake_swallow()) return; ui_nav_show(ui_net_create); }
static void go_stats(lv_event_t *e)    { (void)e; if (wake_swallow()) return; ui_nav_show(ui_stats_create); }
static void go_settings(lv_event_t *e) { (void)e; if (wake_swallow()) return; ui_nav_show(ui_settings_create); }

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

    /* 右：四个入口 */
    const char *names[5] = { "呼号抄收 >", "模拟 QSO >", "电键 >", "联网对练 >", "训练统计 >" };
    lv_event_cb_t cbs[5] = { go_call, go_qso, go_keyer, go_net, go_stats };
    int h = (240 - top) / 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *c = ui_cell(s_scr, names[i], UI_FONT_CN, cbs[i], NULL);
        lv_obj_set_pos(c, 200, top + i * h);
        lv_obj_set_size(c, 120, h);
        lv_obj_set_style_border_side(c, i < 4 ? LV_BORDER_SIDE_BOTTOM : LV_BORDER_SIDE_NONE, 0);
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
