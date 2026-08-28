#include "ui_fist.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_keyer.h"
#include "key_input.h"
#include "cw_settings.h"

/*
 * 时序分析图：把你刚才发报的每一个点划按实际毫秒长度画出来。
 *
 * 为什么这一页有存在的必要：**自己听侧音是听不出手法问题的**。发报的时候
 * 大脑知道自己"想"打什么，会把点划长短不匀自动脑补掉。只有把时长画成图，
 * 或者隔几秒听回放（那是回声陪练干的事），问题才暴露得出来。
 *
 * 判据来自标准时序：点 = 1 单位，划 = 3 单位，符号间隔 = 1 单位。单位长度
 * 由字符速度决定（1200 / wpm 毫秒）。图上两条参考线就是 1 和 3 单位。
 *
 * 数据从 key_input 的键控边沿回调来——那是发报时序的唯一权威来源，和联网
 * 对练打包发出去的是同一批数据。
 */

#define ELEM_MAX 64

static lv_obj_t *s_scr, *s_canvas, *s_summary;
static lv_timer_t *s_timer;

/* 边沿回调在 key_input 的 5ms 任务里跑（core 1，优先级 22），只记数据，
   绝不碰 LVGL。绘图由本页的定时器在 LVGL 线程里做。 */
static volatile int32_t  s_elem[ELEM_MAX];   /* 正=闭合 负=断开，毫秒 */
static volatile int      s_elem_n;
static volatile uint32_t s_last_ms;
static volatile bool     s_have_last;
static volatile bool     s_dirty;

static void on_edge(bool down, uint32_t t_ms, void *user)
{
    (void)user;
    if (!s_have_last) {
        s_have_last = true;
        s_last_ms = t_ms;
        return;
    }
    int32_t dur = (int32_t)(t_ms - s_last_ms);
    s_last_ms = t_ms;
    if (dur <= 0) dur = 1;

    int n = s_elem_n;
    if (n >= ELEM_MAX) {
        /* 满了丢最旧的：看手法只关心最近这一串，越前面的越没参考价值 */
        for (int i = 1; i < ELEM_MAX; i++) s_elem[i - 1] = s_elem[i];
        n = ELEM_MAX - 1;
    }
    /* down 变 true 说明刚结束的是一段断开，记负数 */
    s_elem[n] = down ? -dur : dur;
    s_elem_n = n + 1;
    s_dirty = true;
}

static void draw(void)
{
    lv_obj_clean(s_canvas);

    int n = s_elem_n;
    if (n < 3) {
        lv_obj_t *l = lv_label_create(s_canvas);
        lv_label_set_text(l, "用下面的电键打几个字\n这里会画出每一段的实际长度");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        lv_label_set_text(s_summary, "");
        return;
    }

    const cw_settings_t *cfg = cw_settings_get();
    int unit = 1200 / (cfg->char_wpm ? cfg->char_wpm : 20);

    const int H = 78;                 /* 画布高度 */
    const int scale_ms = unit * 4;    /* 纵轴满量程 = 4 个单位 */

    /* 两条参考线：1 单位（点）和 3 单位（划） */
    const int refs[2] = { unit, unit * 3 };
    for (int r = 0; r < 2; r++) {
        int y = H - refs[r] * H / scale_ms;
        if (y < 0) y = 0;
        lv_obj_t *ln = lv_obj_create(s_canvas);
        lv_obj_remove_style_all(ln);
        lv_obj_set_pos(ln, 0, y);
        lv_obj_set_size(ln, 320, 1);
        lv_obj_set_style_bg_color(ln, UI_GRAY_DOWN, 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
    }

    /* 只画最近能画得下的那些 */
    int bw = 5;
    int max_bars = 320 / bw;
    int start = n > max_bars ? n - max_bars : 0;

    for (int i = start; i < n; i++) {
        int32_t e = s_elem[i];
        bool down = e > 0;
        int32_t ms = down ? e : -e;
        int bh = (int)(ms * H / scale_ms);
        if (bh < 1) bh = 1;
        if (bh > H) bh = H;

        lv_obj_t *b = lv_obj_create(s_canvas);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, (i - start) * bw, H - bh);
        lv_obj_set_size(b, bw - 1, bh);
        /* 键闭合（点划）画实心，断开（间隔）只画边框——一眼能分开 */
        if (down) {
            lv_obj_set_style_bg_color(b, UI_BLACK, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, UI_GRAY_DOWN, 0);
        }
    }

    /*
     * 统计只看键闭合的段：点和划各自的平均长度对不对。
     * 用最短的那一段当"点"的基准来分类，而不是用设置里的 wpm——你实际打的
     * 速度未必等于设置值，拿设置值去判会把"整体偏快但很匀"误判成全错。
     */
    int32_t dit_sum = 0, dah_sum = 0;
    int dit_n = 0, dah_n = 0, min_mark = 0;
    for (int i = 0; i < n; i++) {
        if (s_elem[i] > 2 && (min_mark == 0 || s_elem[i] < min_mark)) min_mark = s_elem[i];
    }
    if (min_mark > 0) {
        for (int i = 0; i < n; i++) {
            if (s_elem[i] <= 2) continue;
            if (s_elem[i] >= min_mark * 2) { dah_sum += s_elem[i]; dah_n++; }
            else { dit_sum += s_elem[i]; dit_n++; }
        }
    }

    if (dit_n && dah_n) {
        int dit_avg = (int)(dit_sum / dit_n);
        int dah_avg = (int)(dah_sum / dah_n);
        /* 划应该是点的 3 倍。用比值而不是绝对毫秒——比值不受你打多快影响。 */
        int ratio10 = dit_avg ? dah_avg * 10 / dit_avg : 0;
        lv_label_set_text_fmt(s_summary, "点 %dms  划 %dms  比 %d.%d（标准 3.0）",
                              dit_avg, dah_avg, ratio10 / 10, ratio10 % 10);
    } else if (dit_n) {
        lv_label_set_text_fmt(s_summary, "点 %dms   还没打出划", (int)(dit_sum / dit_n));
    } else {
        lv_label_set_text(s_summary, "");
    }
}

static void poll(lv_timer_t *t)
{
    (void)t;
    if (!s_scr || !s_dirty) return;
    s_dirty = false;
    draw();
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    s_elem_n = 0;
    s_have_last = false;
    s_dirty = true;
}

static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_keyer_create); }

static void on_delete(lv_event_t *e)
{
    (void)e;
    key_input_set_edge_cb(NULL, NULL);
    key_input_stop();
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_scr = NULL; s_canvas = NULL; s_summary = NULL;
}

lv_obj_t *ui_fist_create(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = ui_topbar(s_scr, "手法", back);
    lv_obj_t *clr = ui_button(bar, "清除", clear_cb, NULL);
    lv_obj_set_size(clr, 56, 24);
    lv_obj_align(clr, LV_ALIGN_RIGHT_MID, -4, 0);

    s_canvas = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_pos(s_canvas, 0, UI_TOPBAR_H);
    lv_obj_set_size(s_canvas, 320, 78);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* 说明行 28px：必须 >= UI_FONT_CN 的行高 26，否则文字上下被裁 */
    s_summary = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_summary, UI_FONT_CN, 0);
    lv_obj_set_pos(s_summary, 6, UI_TOPBAR_H + 78 + 2);
    lv_label_set_text(s_summary, "");

    /* 电键 110px：和电键页、对练页共用同一个构造函数 */
    int py = UI_TOPBAR_H + 78 + 28;
    lv_obj_t *pads[2];
    ui_keyer_paddles(s_scr, py, 240 - py, pads);

    s_elem_n = 0;
    s_have_last = false;
    s_dirty = true;
    key_input_set_edge_cb(on_edge, NULL);
    key_input_start(NULL, NULL);
    s_timer = lv_timer_create(poll, 120, NULL);
    return s_scr;
}
