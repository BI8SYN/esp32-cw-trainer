#include "ui_preview.h"
#include <stdio.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_practice.h"
#include "ui_async.h"
#include "cw_player.h"
#include "koch_engine.h"
#include "morse_table.h"

#define REPEATS 3

static uint8_t s_lesson;
static lv_obj_t *s_scr, *s_status;
static int s_played;

static void play_one(void);

static void on_ui_done(void *arg)
{
    (void)arg;
    if (!s_scr) return;
    s_played++;
    if (s_played < REPEATS) play_one();
    else lv_label_set_text(s_status, "播放完毕");
}

static void player_cb(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_DONE) ui_async_post(on_ui_done, NULL);
}

static void play_one(void)
{
    char t[2] = { koch_new_char(s_lesson), 0 };
    lv_label_set_text_fmt(s_status, "正在播放 %d / %d", s_played + 1, REPEATS);
    /* &s_scr 作为本页在 cw_player 里的会话归属令牌——见 on_delete 的注释 */
    cw_player_play(t, player_cb, &s_scr);
}

/* cw_player 是全局单例：on_delete 可能在下一个页面已经开始播放之后才触发
 * （ui_nav_show 先 build() 新页——新页可能已经调用 cw_player_play()——再异步
 * 删除旧页），这时无条件 cw_player_stop() 会掐掉新会话刚起播的音频，而不是
 * 本页自己那一段。改用 cw_player_stop_if(&s_scr)：只有当前播放会话确实还是
 * 本页自己发起的（token 匹配）才真的停止，否则什么都不做。&s_scr 取本模块
 * 静态变量的地址当令牌——地址稳定、每个页面各不相同，天然满足"区分会话
 * 归属"的要求，不需要另外声明一个专用变量。 */
static void on_delete(lv_event_t *e) { (void)e; cw_player_stop_if(&s_scr); s_scr = NULL; }
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void replay(lv_event_t *e) { (void)e; s_played = 0; play_one(); }
static void start(lv_event_t *e) { (void)e; ui_practice_open_lesson(s_lesson); }

static lv_obj_t *build(void)
{
    s_scr = ui_screen_create();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);
    char title[16]; snprintf(title, sizeof title, "第 %u 课", s_lesson);
    ui_topbar(s_scr, title, back);

    lv_obj_t *hint = lv_label_create(s_scr);
    lv_label_set_text(hint, "新字母");
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 40, 80);

    char nc = koch_new_char(s_lesson);
    lv_obj_t *big = lv_label_create(s_scr);
    lv_obj_set_style_text_font(big, UI_FONT_BIG, 0);
    lv_label_set_text_fmt(big, "%c", nc);
    lv_obj_align(big, LV_ALIGN_TOP_MID, 20, 36);

    lv_obj_t *pat = lv_label_create(s_scr);
    lv_obj_set_style_text_font(pat, UI_FONT_MID, 0);
    lv_label_set_text(pat, morse_table_lookup(nc));
    lv_obj_align(pat, LV_ALIGN_TOP_MID, 20, 120);

    s_status = lv_label_create(s_scr);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 160);

    lv_obj_t *b1 = ui_button(s_scr, "再听一遍", replay, NULL);
    lv_obj_set_width(b1, 130); lv_obj_set_pos(b1, 24, 196);
    lv_obj_t *b2 = ui_button(s_scr, "开始练习", start, NULL);
    lv_obj_set_width(b2, 130); lv_obj_set_pos(b2, 166, 196);

    s_played = 0;
    play_one();
    return s_scr;
}

void ui_preview_open(uint8_t lesson, bool review)
{
    s_lesson = lesson;
    if (review) { ui_practice_open_lesson(lesson); return; }
    ui_nav_show(build);
}
