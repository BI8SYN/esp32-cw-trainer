#include "ui_keyer.h"
#include "ui_assets.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_fist.h"
#include "ui_async.h"
#include "key_input.h"
#include "cw_settings.h"
#include "tone_gen.h"
#include "keyer.h"

#define TEXT_MAX 40   /* 2 行 × 20 字符（等宽 font28 约 16px/字） */

static lv_obj_t *s_scr, *s_text, *s_seq;
/* 点划行是否正被「外接线被挡住」的提示占着 */
static bool s_warn;
/* 电键归属令牌。取本文件某个静态变量的地址即可，只要全局唯一。 */
static const char KEYER_OWNER;
static char s_buf[TEXT_MAX + 1];
static lv_timer_t *s_timer;

static void append_ui(void *arg)
{
    char c = (char)(intptr_t)arg;
    if (!s_scr) return;
    size_t l = strlen(s_buf);
    if (l >= TEXT_MAX) { memmove(s_buf, s_buf + 1, TEXT_MAX); l = TEXT_MAX - 1; }
    s_buf[l] = c; s_buf[l + 1] = 0;
    lv_label_set_text(s_text, s_buf);
}

static void char_cb(char c, void *user) { (void)user; ui_async_post(append_ui, (void *)(intptr_t)c); }

static void poll(lv_timer_t *t)
{
    (void)t;
    const char *p = key_input_pattern();
    /*
     * 点划行此刻可能还占着「某根外接线被门闩挡住」的提示（见 ui_keyer_create）。
     * 提示一直留到你第一次碰键为止——在那之前点划行本来也没内容可显示，让它空着
     * 反而浪费了这台机器唯一能把这件事说出口的位置。
     */
    if (s_warn) {
        if (!*p && !key_input_key_down()) return;
        s_warn = false;
    }
    /* 点划之间加空格显示，如 ". - . ." */
    char shown[24] = ""; size_t n = 0;
    for (; *p && n < sizeof shown - 3; p++) { shown[n++] = *p; shown[n++] = ' '; shown[n] = 0; }
    lv_label_set_text(s_seq, shown);
}

/*
 * 两块桨的分界 x（右块的左边缘）。ui_keyer_paddles() 建的时候记下来，
 * 滑动切换靠它判断手指此刻在哪一半。
 */
static lv_coord_t s_pad_split = 160;
/* 当前按住的是哪一块，-1 = 没有。滑动切换要先松旧的再按新的。 */
static int        s_pad_held = -1;

/*
 * 触摸桨。除了按下/抬起，还处理**按住滑到另一块**。
 *
 * LVGL 在手指按下的那一刻就把「被按对象」锁定了：之后手指滑到另一块，
 * 第二块收不到任何事件，第一块继续收 LV_EVENT_PRESSING。所以只认 PRESSED
 * 的话，滑过去还是原来那一块——实机上就是「按住点滑到划，还是在发点」。
 *
 * 改成每次 PRESSING 都按手指的实时坐标重新判断在哪一半，跨界就松开旧的、
 * 按下新的。对触摸桨来说这是自然的手势：一个手指在两块之间滑，等于快速
 * 换手。真实双桨是靠两根手指，这里没有那个条件。
 */
static void pad_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_pad_held >= 0) { key_input_touch(s_pad_held, false); s_pad_held = -1; }
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int want = (p.x >= s_pad_split) ? 1 : 0;
    if (want == s_pad_held) return;

    if (s_pad_held >= 0) key_input_touch(s_pad_held, false);
    s_pad_held = want;
    key_input_touch(want, true);
}

/*
 * 电键块的高亮状态。**每组桨一份、动态分配**，不能用文件级 static：
 * ui_nav_show() 会先建好新页面再异步删除旧页面，两组桨会短暂同时存在，共享一份
 * 状态时新页面的刷新会去写旧页面已经释放的对象。这正是这个文件此前用
 * s_pad[] 时踩过的坑，那时靠"每条分支各自清零自己不写的下标"来绕，现在从
 * 结构上不再有这个洞。
 */
typedef struct {
    lv_obj_t   *pad[2];
    bool        last[2];
    lv_timer_t *timer;
} pad_hl_t;

static void hl_tick(lv_timer_t *t)
{
    pad_hl_t *h = t->user_data;
    for (int i = 0; i < 2; i++) {
        if (!h->pad[i]) continue;
        bool down = key_input_paddle_down(i);
        /* **只在变化时写。** lv_obj_set_style_bg_color() 会触发样式刷新并让对象
           失效重绘，每 50ms 无脑重写一次会让电键块持续闪烁。 */
        if (down == h->last[i]) continue;
        h->last[i] = down;
        lv_obj_set_style_bg_color(h->pad[i], down ? UI_BRASS_DK : UI_BRASS, 0);
        /* 底色一深，铜面上的刻字就得翻浅，否则按住的时候字直接看不见了 */
        lv_obj_set_style_text_color(h->pad[i], down ? UI_PAPER : UI_BRASS_INK, 0);
    }
}

static void hl_free(lv_event_t *e)
{
    pad_hl_t *h = lv_event_get_user_data(e);
    lv_timer_del(h->timer);
    lv_mem_free(h);
}

/*
 * 把一块普通的抬高面变成黄铜。
 *
 * 电键页是全工程唯一做「真材质」的地方——因为它是唯一一个屏幕元素直接对应
 * 用户手上真实物件（电键桨）的页面，拟物在这里承载真实语义，不是装饰。
 *
 * 材质 = 平涂铜色 + 横向拉丝纹 + 浮雕，**没有渐变**。素材样机第 2 页实测这块
 * 屏连离线抖动过的渐变都有可见台阶；而金属的立体感本来就来自边缘明暗和拉丝
 * 方向，不来自表面的高光过渡。2KB 的 32x32 拉丝瓦片铺满两块桨。
 */
static void brass(lv_obj_t *pad)
{
    lv_obj_set_style_bg_color(pad, UI_BRASS, 0);
    lv_obj_set_style_bg_color(pad, UI_BRASS_DK, LV_STATE_PRESSED);
    lv_obj_set_style_bg_img_src(pad, &ui_grain_brass, 0);
    lv_obj_set_style_bg_img_tiled(pad, true, 0);
    lv_obj_set_style_text_color(pad, UI_BRASS_INK, 0);
    lv_obj_set_style_text_color(pad, UI_PAPER, LV_STATE_PRESSED);
}

void ui_keyer_paddles(lv_obj_t *parent, int y, int h, lv_obj_t *out[2])
{
    const cw_settings_t *cfg = cw_settings_get();
    const bool straight = cfg->keyer_mode == KEYER_MODE_STRAIGHT;
    /* 直键不参与对调，与 key_input.c 的 read_inputs() 保持一致：单个触点没有
       "左右"可言。 */
    const bool swap = cfg->paddle_swap && !straight;

    /* 页面重建时按住状态不能留着，否则会卡出一个永远按下的桨。 */
    s_pad_held = -1;
    s_pad_split = straight ? 320 : 160;   /* 直键整块都算左块 */

    lv_obj_t *pad[2] = { NULL, NULL };
    if (straight) {
        /* 直键只有一个触点，铺成一整条。三个页面都走这里，所以对练页和手法页
           在直键模式下不会再出现"两块桨但右边那块根本不接线"的情况。 */
        pad[0] = ui_cell(parent, "直键（按住）", UI_FONT_CN, NULL, NULL);
        lv_obj_set_pos(pad[0], 0, y);
        lv_obj_set_size(pad[0], 320, h);
        lv_obj_add_event_cb(pad[0], pad_cb, LV_EVENT_ALL, (void *)0);
        brass(pad[0]);
    } else {
        for (int i = 0; i < 2; i++) {
            /* 字体必须是 UI_FONT_CN(font20, 3888 字形)。UI_FONT_MID(font28) 和
             * EN_BIG 都只有 96 个 ASCII 字形、不含中文，"点""划"会变成豆腐块。 */
            /* 开了左右对调，标签跟着换：左块印"划"、右块印"点"。不换的话左块
               印着"点"却发出划，标签就在撒谎。 */
            pad[i] = ui_cell(parent, (i == 0) != swap ? "点" : "划", UI_FONT_CN, NULL, NULL);
            lv_obj_set_pos(pad[i], i * 160, y);
            lv_obj_set_size(pad[i], 160, h);
            lv_obj_add_event_cb(pad[i], pad_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
            brass(pad[i]);
        }
    }

    /*
     * 高亮刷新自带一个定时器，挂在 pad[0] 的删除事件上自毁。放在这里而不是让
     * 各页面自己轮询，是因为电键页、对练页、手法页都铺这同一组桨——按外接电键
     * 时屏幕跟着亮，三个页面一行都不用改就都有了。
     *
     * 触摸按下的高亮不归它管：brass() 已经给 LV_STATE_PRESSED 配了 UI_BRASS_DK，
     * 这里走 LV_PART_MAIN 的默认态，同色，两条路径看起来一样。
     */
    pad_hl_t *hl = lv_mem_alloc(sizeof *hl);
    if (hl) {
        hl->pad[0] = pad[0];
        hl->pad[1] = pad[1];
        hl->last[0] = hl->last[1] = false;
        hl->timer = lv_timer_create(hl_tick, 50, hl);
        lv_obj_add_event_cb(pad[0], hl_free, LV_EVENT_DELETE, hl);
    }

    if (out) { out[0] = pad[0]; out[1] = pad[1]; }
}

static void clear_cb(lv_event_t *e) { (void)e; s_buf[0] = 0; lv_label_set_text(s_text, ""); }
/* 手法分析从电键页进：它讲的是你发出去的时序，走到这一页的人正好在发报。 */
static void go_fist(lv_event_t *e) { (void)e; ui_nav_show(ui_fist_create); }
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void on_delete(lv_event_t *e) { (void)e; key_input_stop_if((void *)&KEYER_OWNER); lv_timer_del(s_timer); s_timer = NULL; s_scr = NULL; }

lv_obj_t *ui_keyer_create(void)
{
    const cw_settings_t *cfg = cw_settings_get();
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    char title[16]; snprintf(title, sizeof title, "%u WPM", cfg->char_wpm);
    lv_obj_t *bar = ui_topbar(s_scr, title, back);
    lv_obj_t *clr = ui_button(bar, "清屏", clear_cb, NULL);
    lv_obj_set_size(clr, 60, 24);
    lv_obj_align(clr, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_t *fst = ui_button(bar, "手法", go_fist, NULL);
    lv_obj_set_size(fst, 60, 24);
    lv_obj_align(fst, LV_ALIGN_RIGHT_MID, -68, 0);

    int top = UI_TOPBAR_H;
    if (!tone_gen_is_ok()) { ui_notice(s_scr, "音频异常，侧音不可用"); top += UI_NOTICE_H; }

    /*
     * 纵向预算（顶栏之下 212px）：解码文本 74 / 点划 26 / 电键 108。
     *
     * 文本高度必须是 74 = 2 行 x UI_FONT_MID 的行高 37。原来只给了 60，
     * 第二行的下缘被裁掉 14px——Q 的尾巴正好在那里，看起来就成了 O。
     * 和配网页输入框、对练页状态带是同一类问题：容器高度必须 >= 内容高度。
     */
    s_text = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_text, UI_FONT_MID, 0);
    lv_obj_set_pos(s_text, 8, top + 2);
    lv_obj_set_size(s_text, 304, 74);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_text, "");
    s_buf[0] = 0;

    /* 点划行改用 UI_FONT_CN（行高 26）：文本要 74、电键要留够 108，
       三者加起来必须 <= 212，点划这行只能用小的。它是转瞬即逝的提示，
       让位给要读的解码文本是对的。 */
    s_seq = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_seq, UI_FONT_CN, 0);
    /* 电键从 84 高之后起点下移，解码文本（74）和电键之间多出一段空当。
       点划行居中放在那段里，而不是贴着文本——不然下面会空出一条没来由的缝。 */
    lv_obj_align(s_seq, LV_ALIGN_TOP_MID, 0, top + 89);
    lv_label_set_text(s_seq, "");

    /* 电键高度和对练页统一，见 ui_keyer.h 的 UI_PADDLE_H。布局（直键一整条 /
       双桨两块 / 对调后的标签）全部由 ui_keyer_paddles() 自己决定，本页不再分支
       ——三个铺电键的页面从此长得一模一样。 */
    ui_keyer_paddles(s_scr, 240 - UI_PADDLE_H, UI_PADDLE_H, NULL);

    key_input_start(char_cb, (void *)&KEYER_OWNER);

    /*
     * 有外接线被门闩挡住就说出来。静默忽略一根坏线，表现是"按了没反应且毫无
     * 线索"，那正是 GPIO27 那次最难查的地方。
     *
     * 写点划行而不是解码文本区：解码文本区用的是 UI_FONT_MID(font28)，只有 96 个
     * ASCII 字形、不含中文，中文会静默渲染成一排豆腐块。点划行是本页唯一能显示
     * 中文、进页面时又为空的位置。
     *
     * 文案带引脚号：开了左右对调之后"点线/划线"指的是哪根物理线会含糊，IO18 /
     * IO23 不会。
     */
    uint8_t blocked = key_input_ext_blocked();
    if (blocked) {
        const char *msg = blocked == 3 ? "外接两根线都一直按下"
                        : blocked == 1 ? "外接点线(IO18)一直按下"
                                       : "外接划线(IO23)一直按下";
        lv_label_set_text(s_seq, msg);
        s_warn = true;
    } else {
        s_warn = false;
    }

    s_timer = lv_timer_create(poll, 50, NULL);
    return s_scr;
}
