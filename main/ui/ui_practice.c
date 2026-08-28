#include "ui_practice.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "ui_common.h"
#include "ui_home.h"
#include "ui_result.h"
#include "ui_async.h"
#include "cw_player.h"
#include "cw_settings.h"
#include "koch_engine.h"
#include "tone_gen.h"

#define FEEDBACK_MS 800
#define SMALL_KBD_MAX 6    /* 键数 ≤ 6 用左右分栏（窄的 150px 右栏只适合很少的键），否则全宽键盘 */
#define SPLIT_KEY_H_MAX 56 /* 分栏模式下单键最大高度，防止行数少时键被拉成竖条 */

typedef enum { PH_PLAYING, PH_INPUT, PH_FEEDBACK } phase_t;

static trainer_t s_tr;
static phase_t s_phase;
static lv_obj_t *s_scr, *s_prog, *s_hint, *s_input, *s_fb, *s_replay;
static lv_obj_t *s_keys[44];
static int s_nkeys;
static lv_obj_t *s_opts[4], *s_rst_opts[4], *s_submit;
static int s_sel_call = -1, s_sel_rst = -1;
static char s_buf[8];
static lv_timer_t *s_fb_timer;

static void start_round(void);

/* ---- 工具 ---- */
static void set_keys_enabled(bool en)
{
    for (int i = 0; i < s_nkeys; i++) {
        if (en) lv_obj_clear_state(s_keys[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s_keys[i], LV_STATE_DISABLED);
    }
    for (int i = 0; i < 4; i++) {
        if (s_opts[i]) { if (en) lv_obj_clear_state(s_opts[i], LV_STATE_DISABLED); else lv_obj_add_state(s_opts[i], LV_STATE_DISABLED); }
        if (s_rst_opts[i]) { if (en) lv_obj_clear_state(s_rst_opts[i], LV_STATE_DISABLED); else lv_obj_add_state(s_rst_opts[i], LV_STATE_DISABLED); }
    }
    if (s_submit) { if (en) lv_obj_clear_state(s_submit, LV_STATE_DISABLED); else lv_obj_add_state(s_submit, LV_STATE_DISABLED); }
    if (s_replay) { if (en) lv_obj_clear_state(s_replay, LV_STATE_DISABLED); else lv_obj_add_state(s_replay, LV_STATE_DISABLED); }
}

static void render_input(void)
{
    if (s_tr.mode != TRAINER_MODE_KOCH) return;
    char shown[16] = "";
    for (int i = 0; i < KOCH_GROUP_LEN; i++) {
        char c = (i < (int)strlen(s_buf)) ? s_buf[i] : '_';
        size_t l = strlen(shown);
        shown[l] = c; shown[l + 1] = ' '; shown[l + 2] = 0;
    }
    lv_label_set_text(s_input, shown);
}

static void set_hint(const char *t) { if (s_hint) lv_label_set_text(s_hint, t); }

/* ---- 播放 ---- */
static void on_play_done_ui(void *arg)
{
    (void)arg;
    if (!s_scr || s_phase != PH_PLAYING) return;
    s_phase = PH_INPUT;
    set_hint(s_tr.mode == TRAINER_MODE_KOCH ? "请输入听到的 5 个字符" :
             s_tr.mode == TRAINER_MODE_CALLSIGN ? "请选择听到的呼号" : "请选择呼号与 RST");
    set_keys_enabled(true);
}

/* cw_player 是全局单例，本页的会话可能被别的页面（例如刚打开的另一个练习/
 * 预告会话）用 cw_player_stop_if() 或 cw_player_stop() 抢停——即便有了会话
 * 归属令牌，仍然可能真的被别人合法地停掉，不只是本页自己 play()/退出时才
 * 会收到 STOPPED。如果对此坐视不理，s_phase 会永远停在 PH_PLAYING，键盘
 * 永远禁用，且 replay_cb 又只在 PH_INPUT 时才生效——页面会彻底卡死，没有任何
 * 用户操作能恢复。这里选择跟 on_play_done_ui 完全一样的恢复方式：直接进入
 * PH_INPUT 并整体解锁，提示文案说明发生了中断。用户可以凭已经听到的部分
 * 直接作答，也可以点"重播"重新完整听一遍——两者都是已有的正常路径，不需要
 * 新增一种"只解锁重播"的中间状态。 */
static void on_play_stopped_ui(void *arg)
{
    (void)arg;
    if (!s_scr || s_phase != PH_PLAYING) return;
    s_phase = PH_INPUT;
    set_hint("播放被中断，可直接作答或点“重播”");
    set_keys_enabled(true);
}

static void player_cb(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_DONE) ui_async_post(on_play_done_ui, NULL);
    else if (ev == CW_PLAYER_EV_STOPPED) ui_async_post(on_play_stopped_ui, NULL);
}

static void play(void)
{
    s_phase = PH_PLAYING;
    set_hint("正在播放中");
    /*
     * 播放期间**不禁用**输入。老手听两三个字符就知道是什么了，没道理按着不让
     * 答完。作答时 submit() 会把播放掐掉（cw_player_stop_if），不会出现「答案
     * 都判完了声音还在响」。
     *
     * 「重播」也不再禁：想从头听一遍是随时该能做的事。
     */
    set_keys_enabled(true);
    /* &s_tr 作为本页在 cw_player 里的会话归属令牌——见 on_delete 的注释 */
    cw_player_play(s_tr.play_text, player_cb, &s_tr);
}

static void replay_cb(lv_event_t *e)
{
    (void)e;
    if (s_phase == PH_FEEDBACK) return;
    /* 正在放就先掐掉，否则两遍会叠在一起响。 */
    cw_player_stop_if(&s_tr);
    play();
}

/* ---- 判分 ---- */
static void after_feedback(lv_timer_t *t)
{
    (void)t;
    s_fb_timer = NULL;
    if (!s_scr) return;
    if (trainer_next(&s_tr)) start_round();
    else ui_result_open(&s_tr);
}

static void submit(const char *answer)
{
    if (s_phase != PH_INPUT && s_phase != PH_PLAYING) return;
    /* 答完就别再响了。播放是异步的（esp_timer 驱动），不掐的话反馈都显示完了
       声音还在放。 */
    cw_player_stop_if(&s_tr);
    bool ok = trainer_submit(&s_tr, answer);
    s_phase = PH_FEEDBACK;
    set_keys_enabled(false);
    lv_obj_clear_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    if (s_input) lv_obj_add_flag(s_input, LV_OBJ_FLAG_HIDDEN);
    /* s_hint 和 s_fb 在呼号/QSO 两种选择模式下（build_choice）画在同一个位置，
       s_input 只在 Koch 模式下存在——上面那行盖不到它们。必须显式把 s_hint
       藏起来，否则反馈文字和提示文字会叠在一起。start_round() 负责下一轮把它
       重新显示出来。 */
    if (s_hint) lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    if (s_tr.mode == TRAINER_MODE_KOCH) {
        if (ok) lv_label_set_text_fmt(s_fb, "正确  %s", s_tr.target);
        else {
            char marks[8]; size_t al = strlen(answer);
            for (int i = 0; i < KOCH_GROUP_LEN; i++) marks[i] = ((size_t)i < al && answer[i] == s_tr.target[i]) ? '-' : '^';
            marks[KOCH_GROUP_LEN] = 0;
            lv_label_set_text_fmt(s_fb, "错误  %s\n      %s\n正确  %s", answer, marks, s_tr.target);
        }
    } else if (s_tr.mode == TRAINER_MODE_CALLSIGN) {
        if (ok) lv_label_set_text(s_fb, "正确");
        else lv_label_set_text_fmt(s_fb, "错误，正确是 %s", s_tr.target);
    } else {
        if (ok) lv_label_set_text(s_fb, "正确");
        else lv_label_set_text_fmt(s_fb, "错误，正确是 %s %s", s_tr.target, s_tr.rst);
    }
    s_fb_timer = lv_timer_create(after_feedback, FEEDBACK_MS, NULL);
    lv_timer_set_repeat_count(s_fb_timer, 1);
}

/* ---- Koch 键盘 ---- */
static void key_cb(lv_event_t *e)
{
    /* 播放期间也收键：判分阶段才拦。 */
    if (s_phase != PH_INPUT && s_phase != PH_PLAYING) return;
    const char *k = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    if (strcmp(k, "退格") == 0) { size_t l = strlen(s_buf); if (l) s_buf[l - 1] = 0; }
    else if (strcmp(k, "确定") == 0) { if (strlen(s_buf) == KOCH_GROUP_LEN) submit(s_buf); return; }
    else if (strlen(s_buf) < KOCH_GROUP_LEN) { size_t l = strlen(s_buf); s_buf[l] = k[0]; s_buf[l + 1] = 0; }
    render_input();
}

/*
 * 键盘分页。
 *
 * 课次靠后时字符集有四十个，加上退格/确定就是四十二个键。可用区是 320x176
 * （顶栏 28 + 提示行 36 之下的全部），按项目一贯守的 44px 触控下限，最多排
 * 8 列 x 4 行 = 32 格——四十二个键根本装不下。原来的做法是继续加列加行，
 * 结果单键被压到 40x29，既点不准又把整块屏幕吃光，上面只剩 36px。
 * 这不是调参能解决的，只能分页。
 *
 * 按**字符类别**切，不按数量硬切：字母一页、数字和符号一页。手不用每次去
 * 找某个字在第几页，而按数量切的话同一个字会随着课次推进换页。
 *
 * 第 40 课字母 26 个 + 三个控制键 = 29，正好在 32 格之内，所以最多两页。
 */
#define KBD_COLS_MAX  8     /* 320 / 8 = 40，再多列就窄到点不准了 */
#define KBD_ROWS_MAX  4     /* 176 / 44：再多一行单键就低于触控下限了 */
#define KBD_KEY_W_MIN 40
#define KBD_KEY_H_MAX 64    /* 行数少时不让单键被拉成横条 */

static char s_pg[2][48];    /* 每页的字符 */
static int  s_pg_n[2];
static int  s_pages, s_page;

static void build_keys(void);

static void page_cb(lv_event_t *e)
{
    (void)e;
    if (s_phase == PH_FEEDBACK) return;
    s_page = (s_page + 1) % s_pages;
    build_keys();
}

/* 按 s_page 铺当前这一页。切换时整块重建——一次点击的事，不值得为它维护
   两套键的显示/隐藏和各自的坐标。 */
static void build_keys(void)
{
    if (!s_scr) return;
    for (int i = 0; i < s_nkeys; i++) if (s_keys[i]) lv_obj_del(s_keys[i]);
    s_nkeys = 0;

    const char *cs = s_pg[s_page];
    int n = s_pg_n[s_page];
    int ctrl = 2 + (s_pages > 1 ? 1 : 0);          /* 退格 确定 [页码] */
    int total = n + ctrl;

    int ky = UI_TOPBAR_H + 36, kh = 240 - ky;

    /*
     * 列数按键数选，不写死。
     *
     * 写死 8 列的话，键少时会排成一行 40x176 的细高条，右边空一截、下面整片
     * 空白——第 4 课只有 5 个字符（加退格确定共 7 个键），就是这个样子。
     *
     * 挑法是「让单键最接近正方形」：从「行数不超过 4 所需的最少列数」试到
     * 上限，取宽高差最小的那个。行数上限保证键高不低于 44px 的触控下限，
     * 列数上限保证键宽不低于 40。
     */
    int lo = (total + KBD_ROWS_MAX - 1) / KBD_ROWS_MAX;
    if (lo < 1) lo = 1;
    if (lo > KBD_COLS_MAX) lo = KBD_COLS_MAX;

    int cols = KBD_COLS_MAX, bestd = 1 << 30;
    for (int c = lo; c <= KBD_COLS_MAX; c++) {
        int r = (total + c - 1) / c;
        int w = 320 / c;
        int h = kh / r;
        if (w < KBD_KEY_W_MIN) continue;
        if (h > KBD_KEY_H_MAX) h = KBD_KEY_H_MAX;
        int d = w > h ? w - h : h - w;
        if (d < bestd) { bestd = d; cols = c; }
    }

    int rows = (total + cols - 1) / cols;
    if (rows > KBD_ROWS_MAX) rows = KBD_ROWS_MAX;
    int cw = 320 / cols, ch = kh / rows;
    /* 封顶之后整块在剩余高度里居中，而不是贴顶把空白全留在下面。 */
    if (ch > KBD_KEY_H_MAX) { ch = KBD_KEY_H_MAX; ky += (kh - rows * ch) / 2; }

    for (int i = 0; i < total && s_nkeys < (int)(sizeof s_keys / sizeof s_keys[0]); i++) {
        /* 24 而不是 8：页码是 "%d/%d"，GCC 按 int 最长 11 位算最坏情况，
           给 8 会被 -Werror=format-truncation 拦下。 */
        char t[24];
        const lv_font_t *f = UI_FONT_CN;
        lv_event_cb_t cb = key_cb;
        if (i < n)            { t[0] = cs[i]; t[1] = 0; f = UI_FONT_MID; }
        else if (i == n)      { strcpy(t, "退格"); }
        else if (i == n + 1)  { strcpy(t, "确定"); }
        else { snprintf(t, sizeof t, "%d/%d", s_page + 1, s_pages); cb = page_cb; }

        lv_obj_t *k = ui_cell(s_scr, t, f, cb, NULL);
        lv_obj_set_pos(k, (i % cols) * cw, ky + (i / cols) * ch);
        lv_obj_set_size(k, cw, ch);
        s_keys[s_nkeys++] = k;
    }
}

static void build_koch(lv_obj_t *s)
{
    char cs[48]; int n = koch_charset(s_tr.lesson, cs);

    /*
     * 一页装得下就不分页。容量 = 8 列 x 4 行减去退格/确定 = 30 个字符，也就是
     * 第 29 课以前都是一页，和以前一样；第 30 课起字符集到 31 个才开始翻页。
     *
     * 这条判断不能省：只按「有没有非字母」分页的话，第 13 课刚学到 "." 就会
     * 为这一个字符单开一页，而那时十几个键一页绰绰有余——凭空多一次翻页。
     */
    s_pg_n[0] = s_pg_n[1] = 0;
    if (n + 2 <= KBD_COLS_MAX * KBD_ROWS_MAX) {
        memcpy(s_pg[0], cs, (size_t)n);
        s_pg_n[0] = n;
    } else {
        /* 装不下才分：字母一页，数字和符号一页。按类别切而不是按数量硬切，
           手不用每次去找某个字在第几页。 */
        for (int i = 0; i < n; i++) {
            int g = (cs[i] >= 'A' && cs[i] <= 'Z') ? 0 : 1;
            if (s_pg_n[g] < (int)sizeof s_pg[0] - 1) s_pg[g][s_pg_n[g]++] = cs[i];
        }
    }
    s_pg[0][s_pg_n[0]] = '\0';
    s_pg[1][s_pg_n[1]] = '\0';
    s_pages = s_pg_n[1] > 0 ? 2 : 1;
    s_page = 0;

    /* 键少的时候仍然走左右分栏：全宽键盘会把几个键拉成一片空旷的大方块。 */
    int total = n + 2;
    bool split = (s_pages == 1 && total <= SMALL_KBD_MAX);

    if (split) {
        s_hint = lv_label_create(s); lv_obj_set_pos(s_hint, 8, UI_TOPBAR_H + 8); lv_obj_set_width(s_hint, 156);
        lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
        s_input = lv_label_create(s); lv_obj_set_style_text_font(s_input, UI_FONT_MID, 0);
        lv_obj_set_pos(s_input, 8, UI_TOPBAR_H + 60);
        s_fb = lv_label_create(s); lv_obj_set_pos(s_fb, 8, UI_TOPBAR_H + 60); lv_obj_set_width(s_fb, 156);
        s_replay = ui_button(s, "重播", replay_cb, NULL); lv_obj_set_width(s_replay, 100); lv_obj_set_pos(s_replay, 8, 196);
        lv_obj_t *line = lv_obj_create(s); lv_obj_set_size(line, 1, 240 - UI_TOPBAR_H); lv_obj_set_pos(line, 168, UI_TOPBAR_H);
        lv_obj_set_style_bg_color(line, UI_BLACK, 0); lv_obj_set_style_border_width(line, 0, 0); lv_obj_set_style_radius(line, 0, 0);
    } else {
        s_hint = lv_label_create(s); lv_obj_set_pos(s_hint, 8, UI_TOPBAR_H + 8); lv_obj_set_width(s_hint, 110);
        s_input = lv_label_create(s); lv_obj_set_style_text_font(s_input, UI_FONT_MID, 0);
        lv_obj_set_pos(s_input, 124, UI_TOPBAR_H + 2);
        s_fb = lv_label_create(s); lv_obj_set_pos(s_fb, 124, UI_TOPBAR_H + 6); lv_obj_set_width(s_fb, 130);
        s_replay = ui_button(s, "重播", replay_cb, NULL); lv_obj_set_size(s_replay, 56, 30); lv_obj_set_pos(s_replay, 258, UI_TOPBAR_H + 3);
    }
    lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);

    s_nkeys = 0;
    if (!split) { build_keys(); return; }

    /* 分栏模式：键盘挤在右边 150px 里，行数少时把整块在剩余高度里居中，
       不然单键会被拉成竖条、下方留一大片空白。 */
    int cols = total <= 4 ? 2 : 3;
    int rows = (total + cols - 1) / cols;
    int ky = UI_TOPBAR_H, kh = 240 - ky;
    int cw = 150 / cols, ch = kh / rows;
    if (ch > SPLIT_KEY_H_MAX) { ch = SPLIT_KEY_H_MAX; ky += (kh - rows * ch) / 2; }

    for (int i = 0; i < total; i++) {
        char t[8];
        if (i < n) { t[0] = cs[i]; t[1] = 0; }
        else strcpy(t, (i == n) ? "退格" : "确定");
        lv_obj_t *k = ui_cell(s, t, (i < n) ? UI_FONT_MID : UI_FONT_CN, key_cb, NULL);
        lv_obj_set_pos(k, 170 + (i % cols) * cw, ky + (i / cols) * ch);
        lv_obj_set_size(k, cw, ch);
        s_keys[s_nkeys++] = k;
    }
}

/* ---- 呼号 / QSO ---- */
static void opt_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT && s_phase != PH_PLAYING) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_tr.mode == TRAINER_MODE_CALLSIGN) { submit(s_tr.options[i]); return; }
    s_sel_call = i;
    for (int k = 0; k < 4; k++) lv_obj_set_style_bg_color(s_opts[k], k == i ? UI_GRAY_DOWN : UI_WHITE, 0);
}
static void rst_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT && s_phase != PH_PLAYING) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel_rst = i;
    for (int k = 0; k < 4; k++) lv_obj_set_style_bg_color(s_rst_opts[k], k == i ? UI_GRAY_DOWN : UI_WHITE, 0);
}
static void submit_qso_cb(lv_event_t *e)
{
    (void)e;
    if (s_sel_call < 0 || s_sel_rst < 0) return;
    char ans[16]; snprintf(ans, sizeof ans, "%s %s", s_tr.options[s_sel_call], s_tr.rst_options[s_sel_rst]);
    submit(ans);
}

static void build_choice(lv_obj_t *s)
{
    int top = UI_TOPBAR_H;
    if (s_tr.charset_fallback) { ui_notice(s, "建议先练 Koch，当前使用全部字母"); top += UI_NOTICE_H; }
    s_hint = lv_label_create(s); lv_obj_set_pos(s_hint, 8, top + 10); lv_obj_set_width(s_hint, 220);
    s_replay = ui_button(s, "重播", replay_cb, NULL); lv_obj_set_size(s_replay, 70, 30); lv_obj_set_pos(s_replay, 242, top + 5);
    s_fb = lv_label_create(s); lv_obj_set_pos(s_fb, 8, top + 10); lv_obj_set_width(s_fb, 230);
    lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    int y = top + 40;

    if (s_tr.mode == TRAINER_MODE_CALLSIGN) {
        int h = (240 - y) / 2;
        for (int i = 0; i < 4; i++) {
            s_opts[i] = ui_cell(s, s_tr.options[i], UI_FONT_MID, opt_cb, (void *)(intptr_t)i);
            lv_obj_set_pos(s_opts[i], (i % 2) * 160, y + (i / 2) * h);
            lv_obj_set_size(s_opts[i], 160, h);
        }
        return;
    }
    lv_obj_t *l1 = lv_label_create(s); lv_label_set_text(l1, "对方呼号"); lv_obj_set_pos(l1, 8, y);
    for (int i = 0; i < 4; i++) {
        s_opts[i] = ui_cell(s, s_tr.options[i], UI_FONT_CN, opt_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(s_opts[i], 8 + i * 77, y + 24); lv_obj_set_size(s_opts[i], 74, 40);
    }
    lv_obj_t *l2 = lv_label_create(s); lv_label_set_text(l2, "RST"); lv_obj_set_pos(l2, 8, y + 70);
    for (int i = 0; i < 4; i++) {
        s_rst_opts[i] = ui_cell(s, s_tr.rst_options[i], UI_FONT_CN, rst_cb, (void *)(intptr_t)i);
        lv_obj_set_pos(s_rst_opts[i], 8 + i * 77, y + 94); lv_obj_set_size(s_rst_opts[i], 74, 40);
    }
    /* 提交按钮挪到"RST"文字这一行、右对齐：这一行从 x≈60 往右是空的。
       呼号选项行下沿在 y+24+40 = y+64（此处 y=68 时为 132），RST 选项行
       上沿在 y+94（y=68 时为 162）——之间只有 30px 高的空当，"RST" 标签
       本身画在 y+70（138）。把按钮放在这 30px 空当里、留 2px 余量：
       高度 26（= y+64+2 到 y+94-2 之间），两侧都不会撞到相邻的选项行。 */
    s_submit = ui_button(s, "提交", submit_qso_cb, NULL);
    lv_obj_set_size(s_submit, 100, 26); lv_obj_set_pos(s_submit, 212, y + 66);
}

/* ---- 页面 ---- */
static void exit_yes(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void back(lv_event_t *e) { (void)e; ui_confirm("退出后本轮不计入进度", "继续练", "退出", exit_yes, NULL); }

static void on_delete(lv_event_t *e)
{
    (void)e;
    /* cw_player 是全局单例：on_delete 可能在下一个页面已经开始播放之后才
     * 触发（ui_nav_show 先 build() 新页——新页可能已经调用 cw_player_play()
     * ——再异步删除旧页），无条件 cw_player_stop() 会掐掉新会话刚起播的
     * 音频。改用 cw_player_stop_if(&s_tr)：只有当前播放会话确实还是本页
     * 自己发起的（token 匹配）才真的停止，否则什么都不做。&s_tr 取本页
     * 会话状态的地址当令牌——天然唯一、天然与本页生命周期绑定。 */
    cw_player_stop_if(&s_tr);
    if (s_fb_timer) { lv_timer_del(s_fb_timer); s_fb_timer = NULL; }
    s_scr = NULL; s_prog = NULL; s_hint = s_input = s_fb = s_replay = s_submit = NULL;
    memset(s_opts, 0, sizeof s_opts); memset(s_rst_opts, 0, sizeof s_rst_opts); s_nkeys = 0;
}

static void start_round(void)
{
    s_buf[0] = 0; s_sel_call = s_sel_rst = -1;
    lv_label_set_text_fmt(s_prog, "%u/%u", s_tr.round + 1, TRAINER_ROUNDS);
    if (s_fb) lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    if (s_hint) lv_obj_clear_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    if (s_input) { lv_obj_clear_flag(s_input, LV_OBJ_FLAG_HIDDEN); render_input(); }
    /* 选项文本随每轮更新 */
    for (int i = 0; i < 4; i++) {
        if (s_opts[i]) { lv_label_set_text(lv_obj_get_child(s_opts[i], 0), s_tr.options[i]); lv_obj_set_style_bg_color(s_opts[i], UI_WHITE, 0); }
        if (s_rst_opts[i]) { lv_label_set_text(lv_obj_get_child(s_rst_opts[i], 0), s_tr.rst_options[i]); lv_obj_set_style_bg_color(s_rst_opts[i], UI_WHITE, 0); }
    }
    play();
}

static lv_obj_t *build(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    char title[16];
    if (s_tr.mode == TRAINER_MODE_KOCH) snprintf(title, sizeof title, "第 %u 课", s_tr.lesson);
    else strcpy(title, s_tr.mode == TRAINER_MODE_CALLSIGN ? "呼号抄收" : "模拟 QSO");
    lv_obj_t *bar = ui_topbar(s_scr, title, back);
    s_prog = lv_label_create(bar);
    lv_obj_align(s_prog, LV_ALIGN_RIGHT_MID, -8, 0);

    if (!tone_gen_is_ok()) ui_notice(s_scr, "音频异常，无法播放，请退出重试");

    if (s_tr.mode == TRAINER_MODE_KOCH) build_koch(s_scr);
    else build_choice(s_scr);

    start_round();
    return s_scr;
}

static void open_with(trainer_mode_t mode, uint8_t lesson)
{
    trainer_start(&s_tr, mode, lesson, cw_settings_get()->callsign, (uint32_t)esp_timer_get_time());
    ui_nav_show(build);
}

void ui_practice_open(trainer_mode_t mode) { open_with(mode, cw_settings_get()->koch_lesson); }
void ui_practice_open_lesson(uint8_t lesson) { open_with(TRAINER_MODE_KOCH, lesson); }
