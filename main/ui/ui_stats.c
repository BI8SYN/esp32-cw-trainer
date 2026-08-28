#include "ui_stats.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "stats_store.h"
#include "koch_engine.h"

/*
 * 训练统计。两个页签共用一块画布：
 *
 *   错字热力图 —— 哪些字母你老抄错。Koch 的价值在于逐个加字母，但它不会
 *                 告诉你哪个字母始终没学会；错误率排出来就一目了然。
 *   进度曲线   —— 最近若干次结算的正确率。单看一次成绩没有意义，看趋势
 *                 才知道是在进步还是在原地打转。
 *
 * 两者都是纯展示，数据来自 core/cw_stats.c（主机有测试覆盖）。
 */

#define TAB_H     (240 - UI_TOPBAR_H)
/*
 * 画布的实际可用宽度。
 *
 * 画布放在 x=56、宽 320-56=264，左边还画了一条 1px 的分隔线，所以子对象能用的
 * 只有 263。两张图原来都是拿 320 去算坐标的——那是整屏的宽度，不是这个容器的
 * ——于是参考线、柱子、百分比标签统统画到屏幕外面去了。
 */
#define TAB_X     56
#define CANVAS_W  (320 - TAB_X - 1)
#define WORST_MAX 8

static lv_obj_t *s_scr, *s_canvas, *s_tabs[2];
static int s_tab;

static lv_obj_t *build(void);
static void tab_cb(lv_event_t *e);

/* ---------------------------------------------------------------- 热力图 */

/*
 * 用横向条形而不是真的"热力方块"：320x240 上 40 个字母铺成网格，每格只有
 * 30px 见方，字母和数字都塞不下，看着热闹但读不出信息。排序后的条形图能
 * 直接回答"我最该练哪个字母"这个唯一有用的问题。
 */
static void draw_heat(lv_obj_t *p)
{
    const cw_stats_t *st = stats_store_get();
    char ch[WORST_MAX];
    uint8_t pct[WORST_MAX];
    int n = cw_stats_worst(st, ch, pct, WORST_MAX);

    if (n == 0) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text(l, "还没有错字记录\n练几课 Koch 再来看");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        return;
    }

    int row_h = 24;
    for (int i = 0; i < n; i++) {
        int y = 4 + i * row_h;

        lv_obj_t *c = lv_label_create(p);
        lv_label_set_text_fmt(c, "%c", ch[i]);
        lv_obj_set_pos(c, 6, y);

        /* 条形从 x=30 起，右边给百分比留 48px，剩下的才是 100% 对应的长度。
           原来写死 180，加上 x=30 是按整屏 320 估的，在 263 宽的画布里会顶到
           百分比标签上。 */
        lv_obj_t *bar = lv_obj_create(p);
        lv_obj_remove_style_all(bar);
        const int bar_max = CANVAS_W - 30 - 52;
        int w = 4 + pct[i] * bar_max / 100;
        lv_obj_set_pos(bar, 30, y + 4);
        lv_obj_set_size(bar, w, 16);
        lv_obj_set_style_bg_color(bar, UI_GRAY_DOWN, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, UI_BLACK, 0);

        lv_obj_t *v = lv_label_create(p);
        lv_label_set_text_fmt(v, "%u%%", pct[i]);
        lv_obj_set_pos(v, CANVAS_W - 48, y);
    }
}

/* ---------------------------------------------------------------- 进度曲线 */

static void draw_curve(lv_obj_t *p)
{
    const cw_stats_t *st = stats_store_get();
    /* lesson[] 现在没人读——说明行不再显示课次，见下面 tip 的注释。
       但 cw_stats_history() 的签名要求这个出参，不能传 NULL。 */
    uint8_t pct[CW_STATS_HIST], lesson[CW_STATS_HIST];
    int n = cw_stats_history(st, pct, lesson, CW_STATS_HIST);
    (void)lesson;

    if (n < 2) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text(l, "至少练两轮才画得出趋势");
        lv_obj_center(l);
        return;
    }

    /*
     * 绘图区：左边 32px 给刻度，右边留 8px，底部留 32px 给说明那一行。
     *
     * 宽度按 CANVAS_W 算而不是 320——320 是整屏，这里是 263 宽的画布，按整屏算
     * 会让参考线和最右几根柱子跑到屏幕外面。底部留 32 而不是 24：说明文字本身
     * 26px 高，留 24 的话它的下缘会超出画布 6px。
     */
    const int x0 = 32, y0 = 8, w = CANVAS_W - x0 - 8, h = TAB_H - y0 - 32;

    /* 90% 达标线——Koch 解锁下一课的门槛，是这张图唯一有意义的参考线 */
    lv_obj_t *ref = lv_obj_create(p);
    lv_obj_remove_style_all(ref);
    lv_obj_set_pos(ref, x0, y0 + h - h * KOCH_PASS_PERCENT / 100);
    lv_obj_set_size(ref, w, 1);
    lv_obj_set_style_bg_color(ref, UI_GRAY_DOWN, 0);
    lv_obj_set_style_bg_opa(ref, LV_OPA_COVER, 0);

    lv_obj_t *rl = lv_label_create(p);
    lv_label_set_text(rl, "90");
    lv_obj_set_style_text_font(rl, UI_FONT_CN, 0);
    lv_obj_set_pos(rl, 2, y0 + h - h * KOCH_PASS_PERCENT / 100 - 12);

    /*
     * 用竖条而不是折线：LVGL 的 chart 控件会带进一套自己的样式和坐标系统，
     * 而这里只要"最近 N 次谁高谁低"这一个信息。竖条在 32 个数据点、288px
     * 宽下每根 9px，看得清，也不需要处理折线的抗锯齿。
     */
    int bw = w / n;
    if (bw < 2) bw = 2;
    for (int i = 0; i < n; i++) {
        int bh = pct[i] * h / 100;
        if (bh < 1) bh = 1;
        lv_obj_t *b = lv_obj_create(p);
        lv_obj_remove_style_all(b);
        lv_obj_set_pos(b, x0 + i * bw, y0 + h - bh);
        lv_obj_set_size(b, bw > 2 ? bw - 1 : bw, bh);
        lv_obj_set_style_bg_color(b, UI_GRAY_DOWN, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, UI_BLACK, 0);
    }

    /* 说明只留「多少次」和「最新多少」：原来还带着「（第 N 课）」，整句在
       font20 下要三百多像素，而这里只有 w 这么宽，尾巴会甩出屏幕。课次在首页
       和结算页都写着，不必在这里重复。宽度设死并用 DOT 兜底。 */
    lv_obj_t *tip = lv_label_create(p);
    /* 分隔用中文逗号，不用「·」——那个字符不在裁剪字库里，上屏会是空白且
       不报编译错，check_charset.py 才拦得住。 */
    lv_label_set_text_fmt(tip, "最近 %d 次，最新 %u%%", n, pct[n - 1]);
    lv_obj_set_size(tip, w, 26);
    lv_label_set_long_mode(tip, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(tip, x0, y0 + h + 4);
}

/* ---------------------------------------------------------------- 骨架 */

static void back_home(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void on_delete(lv_event_t *e) { (void)e; s_scr = NULL; s_canvas = NULL; }

static void fill(void)
{
    lv_obj_clean(s_canvas);
    if (s_tab == 0) draw_heat(s_canvas);
    else draw_curve(s_canvas);
    for (int i = 0; i < 2; i++)
        lv_obj_set_style_bg_color(s_tabs[i], i == s_tab ? UI_GRAY_DOWN : UI_WHITE, 0);
}

static void tab_cb(lv_event_t *e)
{
    s_tab = (int)(intptr_t)lv_event_get_user_data(e);
    fill();
}

static lv_obj_t *build(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    ui_topbar(s_scr, "训练统计", back_home);

    const char *names[2] = { "错字", "进度" };
    int th = TAB_H / 2;
    for (int i = 0; i < 2; i++) {
        s_tabs[i] = ui_cell(s_scr, names[i], UI_FONT_CN, tab_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(s_tabs[i], 0, UI_TOPBAR_H + i * th);
        lv_obj_set_size(s_tabs[i], 56, th);
    }

    s_canvas = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_set_pos(s_canvas, 56, UI_TOPBAR_H);
    lv_obj_set_size(s_canvas, 320 - 56, TAB_H);
    lv_obj_set_style_border_width(s_canvas, 1, 0);
    lv_obj_set_style_border_side(s_canvas, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_color(s_canvas, UI_BLACK, 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    fill();
    return s_scr;
}

lv_obj_t *ui_stats_create(void) { return build(); }
