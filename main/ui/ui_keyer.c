#include "ui_keyer.h"
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_home.h"
#include "ui_async.h"
#include "key_input.h"
#include "cw_settings.h"
#include "tone_gen.h"
#include "keyer.h"

#define TEXT_MAX 40   /* 2 行 × 20 字符（等宽 font28 约 16px/字） */

static lv_obj_t *s_scr, *s_text, *s_seq, *s_pad[2];
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
    /* 点划之间加空格显示，如 ". - . ." */
    char shown[24] = ""; size_t n = 0;
    for (; *p && n < sizeof shown - 3; p++) { shown[n++] = *p; shown[n++] = ' '; shown[n] = 0; }
    lv_label_set_text(s_seq, shown);
    bool down = key_input_key_down();
    if (cw_settings_get()->key_input == CW_KEY_INPUT_EXT)
        for (int i = 0; i < 2; i++) if (s_pad[i]) lv_obj_set_style_bg_color(s_pad[i], down ? UI_GRAY_DOWN : UI_WHITE, 0);
}

static void pad_cb(lv_event_t *e)
{
    int paddle = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) key_input_touch(paddle, true);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) key_input_touch(paddle, false);
}

static void clear_cb(lv_event_t *e) { (void)e; s_buf[0] = 0; lv_label_set_text(s_text, ""); }
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void on_delete(lv_event_t *e) { (void)e; key_input_stop(); lv_timer_del(s_timer); s_timer = NULL; s_scr = NULL; s_pad[0] = s_pad[1] = NULL; }

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

    int top = UI_TOPBAR_H;
    if (!tone_gen_is_ok()) { ui_notice(s_scr, "音频异常，侧音不可用"); top += 22; }

    s_text = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_text, UI_FONT_MID, 0);
    lv_obj_set_pos(s_text, 8, top + 4);
    lv_obj_set_size(s_text, 304, 60);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_text, "");
    s_buf[0] = 0;

    s_seq = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_seq, UI_FONT_MID, 0);
    lv_obj_align(s_seq, LV_ALIGN_TOP_MID, 0, top + 66);
    lv_label_set_text(s_seq, "");

    /* s_pad[] 是跨页面存活的文件级 static；下面三条布局分支里，仅"外接+直键"
     * 分支只写 s_pad[0]（循环上限 1），不会碰 s_pad[1]。如果上一次进入本页时
     * 走的是双桨分支（触摸或外接），s_pad[1] 会指向那次创建、现在已经随旧屏幕
     * 一起被删除的对象；不在这里清零的话它就是一个悬空指针，一旦这次又是外接
     * 模式，poll() 里 "if (s_pad[i])" 判断会认为它还有效，对已释放对象调用
     * lv_obj_set_style_bg_color——use-after-free。三条分支各自负责写自己要用的
     * 下标，这里统一先清零，保证没被某条分支写到的下标一定是 NULL 而不是历史
     * 残留值。 */
    s_pad[0] = s_pad[1] = NULL;

    int py = 120, ph = 240 - py;
    bool straight = cfg->keyer_mode == KEYER_MODE_STRAIGHT;
    bool ext = cfg->key_input == CW_KEY_INPUT_EXT;
    if (ext) {
        lv_obj_t *l = lv_label_create(s_scr); lv_label_set_text(l, "外接电键已就绪"); lv_obj_align(l, LV_ALIGN_TOP_MID, 0, py + 8);
        for (int i = 0; i < (straight ? 1 : 2); i++) {
            s_pad[i] = ui_cell(s_scr, straight ? "键" : (i == 0 ? "点" : "划"), UI_FONT_CN, NULL, NULL);
            lv_obj_set_size(s_pad[i], 90, 60);
            lv_obj_set_pos(s_pad[i], straight ? 115 : 60 + i * 110, py + 44);
            lv_obj_clear_flag(s_pad[i], LV_OBJ_FLAG_CLICKABLE);
        }
    } else if (straight) {
        s_pad[0] = ui_cell(s_scr, "直键（按住）", UI_FONT_CN, NULL, NULL);
        lv_obj_set_pos(s_pad[0], 0, py); lv_obj_set_size(s_pad[0], 320, ph);
        lv_obj_add_event_cb(s_pad[0], pad_cb, LV_EVENT_ALL, (void *)0);
        s_pad[1] = NULL;
    } else {
        for (int i = 0; i < 2; i++) {
            s_pad[i] = ui_cell(s_scr, i == 0 ? "点" : "划", UI_FONT_CN, NULL, NULL);
            lv_obj_set_pos(s_pad[i], i * 160, py); lv_obj_set_size(s_pad[i], 160, ph);
            lv_obj_add_event_cb(s_pad[i], pad_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
        }
    }

    key_input_start(char_cb, NULL);
    s_timer = lv_timer_create(poll, 50, NULL);
    return s_scr;
}
