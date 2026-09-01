#include "ui_fist.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_keyer.h"
#include "key_input.h"
#include "cw_settings.h"
#include "keyer.h"

/*
 * 时序分析图：把你刚才发报的每一段按实际毫秒长度画出来。
 *
 * 为什么这一页有存在的必要：**自己听侧音是听不出手法问题的**。发报的时候
 * 大脑知道自己"想"打什么，会把不匀自动脑补掉。只有把时长画成图，或者隔
 * 几秒听回放（那是回声陪练干的事），问题才暴露得出来。
 *
 * **量什么取决于电键类型，这一点是本页的核心。**
 *
 *   直键   —— keyer.c 里那条 `k->key_out = k->dit_in` 分支是原样透传，
 *              每一段的时长都是人控制的。量点和划：标准是 1 : 3。
 *
 *   自动键 —— keyer.c 用 `dit_ms` / `dit_ms * 3` 生成点划，符号间隔也是
 *              定长。这些全是机器算的，画出来是一排一模一样的条，量它
 *              没有任何意义。
 *
 *              但**字与字之间的间隔完全由人控制**——这恰恰是电键手最常
 *              被批评的地方：点划再标准，字间距忽长忽短一样难抄。所以
 *              自动键下量的是间隔：字间隔标准 3 单位，词间隔 7 单位。
 *
 * 数据从 key_input 的键控边沿回调来——那是发报时序的唯一权威来源，和联网
 * 对练打包发出去的是同一批数据。负数元素里约 1 单位的是机器插的符号间隔，
 * 大于 2 单位的才是人停顿出来的。
 */

#define ELEM_MAX 64

static lv_obj_t *s_scr, *s_canvas, *s_summary;
static const char FIST_OWNER;   /* 电键归属令牌，见 key_input_stop_if() */
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
        lv_label_set_text(l, cw_settings_get()->keyer_mode == KEYER_MODE_STRAIGHT
                          ? "用下面的电键打几个字\n看点和划的长度是否匀"
                          : "用下面的电键打几个字\n自动键点划由机器生成，这里看字间隔");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        lv_label_set_text(s_summary, "");
        return;
    }

    const cw_settings_t *cfg = cw_settings_get();
    bool straight = cfg->keyer_mode == KEYER_MODE_STRAIGHT;

    /*
     * 基准单位取实际打出来的最短一段，而不是设置里的 wpm——你实际的速度
     * 未必等于设置值，拿设置值去判会把"整体偏快但很匀"误判成全错。
     */
    int32_t unit = 0;
    for (int i = 0; i < n; i++) {
        if (s_elem[i] > 2 && (unit == 0 || s_elem[i] < unit)) unit = s_elem[i];
    }
    if (unit <= 0) unit = 1200 / (cfg->char_wpm ? cfg->char_wpm : 20);

    const int H = 78;
    /* 直键关心 1~3 单位的点划，自动键关心 3~7 单位的间隔，量程不同 */
    const int scale_ms = straight ? unit * 4 : unit * 8;

    /* 参考线：直键画 1 和 3 单位（点、划），自动键画 3 和 7 单位（字、词间隔） */
    const int refs[2] = { straight ? unit : unit * 3, straight ? unit * 3 : unit * 7 };
    for (int r = 0; r < 2; r++) {
        int y = H - refs[r] * H / scale_ms;
        if (y < 0) y = 0;
        lv_obj_t *ln = lv_obj_create(s_canvas);
        lv_obj_remove_style_all(ln);
        lv_obj_set_pos(ln, 0, y);
        lv_obj_set_size(ln, 320, 1);
        lv_obj_set_style_bg_color(ln, UI_GRID, 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
    }

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
        /* 键闭合画实心、断开画空心。自动键下要看的是空心那些。 */
        if (down) {
            lv_obj_set_style_bg_color(b, UI_INK, 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_border_width(b, 1, 0);
            lv_obj_set_style_border_color(b, UI_DISABLED, 0);
        }
    }

    if (straight) {
        /* 直键：点和划的平均长度与比值。用比值而不是绝对毫秒——比值不受
           你打多快影响。 */
        int32_t dit_sum = 0, dah_sum = 0;
        int dit_n = 0, dah_n = 0;
        for (int i = 0; i < n; i++) {
            if (s_elem[i] <= 2) continue;
            if (s_elem[i] >= unit * 2) { dah_sum += s_elem[i]; dah_n++; }
            else { dit_sum += s_elem[i]; dit_n++; }
        }
        if (dit_n && dah_n) {
            int dit_avg = (int)(dit_sum / dit_n);
            int dah_avg = (int)(dah_sum / dah_n);
            int ratio10 = dit_avg ? dah_avg * 10 / dit_avg : 0;
            lv_label_set_text_fmt(s_summary, "点 %dms 划 %dms 比 %d.%d（标准 3.0）",
                                  dit_avg, dah_avg, ratio10 / 10, ratio10 % 10);
        } else if (dit_n) {
            lv_label_set_text_fmt(s_summary, "点 %dms   还没打出划", (int)(dit_sum / dit_n));
        } else {
            lv_label_set_text(s_summary, "");
        }
        return;
    }

    /*
     * 自动键：点划是机器生成的定长，量它没意义。量间隔。
     *
     * 负数元素里约 1 单位的是 keyer 自己插的符号间隔（机器的，跳过），
     * 大于 2 单位的才是人松开按键停顿出来的——那才是字间隔和词间隔。
     * 字间隔标准 3 单位，词间隔 7 单位；用 5 单位作为两者的分界。
     */
    int32_t gap_sum = 0, gmin = 0, gmax = 0;
    int gap_n = 0;
    for (int i = 0; i < n; i++) {
        if (s_elem[i] >= 0) continue;
        int32_t g = -s_elem[i];
        if (g < unit * 2) continue;            /* 机器插的符号间隔 */
        if (g >= unit * 5) continue;           /* 词间隔，另算 */
        gap_sum += g;
        gap_n++;
        if (gmin == 0 || g < gmin) gmin = g;
        if (g > gmax) gmax = g;
    }

    if (gap_n >= 2) {
        /* 用「多少个单位」而不是毫秒：单位数直接对标准，毫秒还得心算。 */
        int avg10 = (int)(gap_sum * 10 / gap_n / unit);
        int min10 = (int)(gmin * 10 / unit);
        int max10 = (int)(gmax * 10 / unit);
        lv_label_set_text_fmt(s_summary, "字间隔 %d.%d 单位（标准 3.0） 最短 %d.%d 最长 %d.%d",
                              avg10 / 10, avg10 % 10, min10 / 10, min10 % 10,
                              max10 / 10, max10 % 10);
    } else {
        lv_label_set_text(s_summary, "自动键量的是字间隔，多打几个字");
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
    /* 只在电键还归本页时才停。ui_nav_show 先建新页再删旧页，无条件停止
       会掐掉新页刚启动的电键——见 key_input_stop_if() 的说明。 */
    key_input_stop_if((void *)&FIST_OWNER);
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    s_scr = NULL; s_canvas = NULL; s_summary = NULL;
}

lv_obj_t *ui_fist_create(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = ui_topbar(s_scr, "手法", back);
/* 顶栏按钮统一用 ui_chip：原来是 ui_button 写死高 24，而 UI_FONT_CN 行高
       是 26——内容比容器高，LVGL 当溢出处理，文字下偏还会打开滚动。ui_chip
       的高度就等于行高，在 32px 顶栏里上下各留 2px。 */
    lv_obj_t *clr = ui_chip(bar, "清除", clear_cb, NULL);
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

    /* 电键：和电键页、对练页共用同一个构造函数，布局由它按设置决定 */
    int py = UI_TOPBAR_H + 78 + 28;
    ui_keyer_paddles(s_scr, py, 240 - py, NULL);

    s_elem_n = 0;
    s_have_last = false;
    s_dirty = true;
    /* 必须先 start 再挂边沿：start 会清掉上一个页面留下的边沿回调。 */
    key_input_start(NULL, (void *)&FIST_OWNER);
    key_input_set_edge_cb(on_edge, NULL);
    s_timer = lv_timer_create(poll, 120, NULL);
    return s_scr;
}
