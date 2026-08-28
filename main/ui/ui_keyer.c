#include "ui_keyer.h"
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

static lv_obj_t *s_scr, *s_text, *s_seq, *s_pad[2];
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

void ui_keyer_paddles(lv_obj_t *parent, int y, int h, lv_obj_t *out[2])
{
    for (int i = 0; i < 2; i++) {
        /* 字体必须是 UI_FONT_CN(font20, 3888 字形)。UI_FONT_MID(font28) 和
         * EN_BIG 都只有 96 个 ASCII 字形、不含中文，"点""划"会变成豆腐块。 */
        out[i] = ui_cell(parent, i == 0 ? "点" : "划", UI_FONT_CN, NULL, NULL);
        lv_obj_set_pos(out[i], i * 160, y);
        lv_obj_set_size(out[i], 160, h);
        lv_obj_add_event_cb(out[i], pad_cb, LV_EVENT_ALL, (void *)(intptr_t)i);
    }
}

static void clear_cb(lv_event_t *e) { (void)e; s_buf[0] = 0; lv_label_set_text(s_text, ""); }
/* 手法分析从电键页进：它讲的是你发出去的时序，走到这一页的人正好在发报。 */
static void go_fist(lv_event_t *e) { (void)e; ui_nav_show(ui_fist_create); }
static void back(lv_event_t *e) { (void)e; ui_nav_show(ui_home_create); }
static void on_delete(lv_event_t *e) { (void)e; key_input_stop_if((void *)&KEYER_OWNER); lv_timer_del(s_timer); s_timer = NULL; s_scr = NULL; s_pad[0] = s_pad[1] = NULL; }

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
    lv_obj_align(s_seq, LV_ALIGN_TOP_MID, 0, top + 78);
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

    /* 132 起，剩 108px 给电键——远超 44px 触控下限，同时给上面的解码文本
       腾出完整的两行高度。 */
    int py = 132, ph = 240 - py;
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
        ui_keyer_paddles(s_scr, py, ph, s_pad);
    }

    key_input_start(char_cb, (void *)&KEYER_OWNER);
    s_timer = lv_timer_create(poll, 50, NULL);
    return s_scr;
}
