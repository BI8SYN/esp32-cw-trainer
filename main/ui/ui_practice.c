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
#define SMALL_KBD_MAX 12   /* 键数 ≤ 12 用左右分栏，否则全宽键盘 */

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

static void player_cb(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_DONE) ui_async_post(on_play_done_ui, NULL);
}

static void play(void)
{
    s_phase = PH_PLAYING;
    set_hint("正在播放中");
    set_keys_enabled(false);
    cw_player_play(s_tr.play_text, player_cb, NULL);
}

static void replay_cb(lv_event_t *e) { (void)e; if (s_phase == PH_INPUT) play(); }

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
    if (s_phase != PH_INPUT) return;
    bool ok = trainer_submit(&s_tr, answer);
    s_phase = PH_FEEDBACK;
    set_keys_enabled(false);
    lv_obj_clear_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
    if (s_input) lv_obj_add_flag(s_input, LV_OBJ_FLAG_HIDDEN);
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
    if (s_phase != PH_INPUT) return;
    const char *k = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    if (strcmp(k, "退格") == 0) { size_t l = strlen(s_buf); if (l) s_buf[l - 1] = 0; }
    else if (strcmp(k, "确定") == 0) { if (strlen(s_buf) == KOCH_GROUP_LEN) submit(s_buf); return; }
    else if (strlen(s_buf) < KOCH_GROUP_LEN) { size_t l = strlen(s_buf); s_buf[l] = k[0]; s_buf[l + 1] = 0; }
    render_input();
}

static void build_koch(lv_obj_t *s)
{
    char cs[48]; int n = koch_charset(s_tr.lesson, cs);
    int total = n + 2;
    bool split = total <= SMALL_KBD_MAX;
    int cols = split ? 4 : (total <= 24 ? 6 : 8);
    int rows = (total + cols - 1) / cols;
    int kx = split ? 170 : 0, ky = split ? UI_TOPBAR_H : UI_TOPBAR_H + 36;
    int kw = split ? 150 : 320, kh = 240 - ky;
    int cw = kw / cols, ch = kh / rows;

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
    for (int i = 0; i < total; i++) {
        char t[8];
        if (i < n) { t[0] = cs[i]; t[1] = 0; }
        else strcpy(t, (i == n) ? "退格" : "确定");
        lv_obj_t *k = ui_cell(s, t, (i < n) ? UI_FONT_MID : UI_FONT_CN, key_cb, NULL);
        lv_obj_set_pos(k, kx + (i % cols) * cw, ky + (i / cols) * ch);
        lv_obj_set_size(k, cw, ch);
        s_keys[s_nkeys++] = k;
    }
}

/* ---- 呼号 / QSO ---- */
static void opt_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_tr.mode == TRAINER_MODE_CALLSIGN) { submit(s_tr.options[i]); return; }
    s_sel_call = i;
    for (int k = 0; k < 4; k++) lv_obj_set_style_bg_color(s_opts[k], k == i ? UI_GRAY_DOWN : UI_WHITE, 0);
}
static void rst_cb(lv_event_t *e)
{
    if (s_phase != PH_INPUT) return;
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
    if (s_tr.charset_fallback) { ui_notice(s, "建议先练 Koch，当前使用全部字母"); top += 22; }
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
    s_submit = ui_button(s, "提交", submit_qso_cb, NULL);
    lv_obj_set_width(s_submit, 100); lv_obj_set_pos(s_submit, 212, 200);
}

/* ---- 页面 ---- */
static void exit_yes(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void back(lv_event_t *e) { (void)e; ui_confirm("退出后本轮不计入进度", "继续练", "退出", exit_yes, NULL); }

static void on_delete(lv_event_t *e)
{
    (void)e;
    cw_player_stop();
    if (s_fb_timer) { lv_timer_del(s_fb_timer); s_fb_timer = NULL; }
    s_scr = NULL; s_hint = s_input = s_fb = s_replay = s_submit = NULL;
    memset(s_opts, 0, sizeof s_opts); memset(s_rst_opts, 0, sizeof s_rst_opts); s_nkeys = 0;
}

static void start_round(void)
{
    s_buf[0] = 0; s_sel_call = s_sel_rst = -1;
    lv_label_set_text_fmt(s_prog, "%u/%u", s_tr.round + 1, TRAINER_ROUNDS);
    if (s_fb) lv_obj_add_flag(s_fb, LV_OBJ_FLAG_HIDDEN);
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
