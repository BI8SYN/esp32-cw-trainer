#include "ui_common.h"

void ui_nav_show(ui_screen_builder_t builder)
{
    lv_obj_t *old = lv_scr_act();
    lv_obj_t *scr = builder();
    lv_scr_load(scr);
    /* 通常在旧屏幕某个子对象的事件回调里被调用，必须异步删除 */
    if (old && old != scr) lv_obj_del_async(old);
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_WHITE, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(scr, UI_BLACK, 0);
    lv_obj_set_style_text_font(scr, UI_FONT_CN, 0);
    return scr;
}

static void plain(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *bar = lv_obj_create(parent);
    plain(bar);
    lv_obj_set_size(bar, LV_PCT(100), UI_TOPBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_WHITE, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, UI_BLACK, 0);

    int x = 6;
    if (back_cb) {
        lv_obj_t *back = lv_label_create(bar);
        lv_label_set_text(back, "<");
        lv_obj_set_style_text_font(back, UI_FONT_MID, 0);
        lv_obj_set_size(back, 36, UI_TOPBAR_H);
        lv_obj_set_style_pad_left(back, 6, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(back, 8);
        lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
        x = 40;
    }
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    return bar;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    plain(b);
    lv_obj_set_style_bg_color(b, UI_WHITE, 0);
    lv_obj_set_style_bg_color(b, UI_GRAY_DOWN, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, UI_WHITE, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, UI_BLACK, 0);
    lv_obj_set_style_border_color(b, UI_GRAY_DOWN, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, UI_BLACK, 0);
    lv_obj_set_style_text_color(b, UI_GRAY_DOWN, LV_STATE_DISABLED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = make_btn(parent, text, UI_FONT_CN, cb, user);
    lv_obj_set_height(b, 36);
    return b;
}

lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    return make_btn(parent, text, font, cb, user);
}

lv_obj_t *ui_notice(lv_obj_t *parent, const char *text)
{
    lv_obj_t *n = lv_obj_create(parent);
    plain(n);
    lv_obj_set_size(n, LV_PCT(100), 22);
    lv_obj_set_pos(n, 0, UI_TOPBAR_H);
    lv_obj_set_style_bg_color(n, UI_GRAY_BG, 0);
    lv_obj_set_style_border_width(n, 1, 0);
    lv_obj_set_style_border_side(n, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(n, UI_BLACK, 0);
    lv_obj_t *l = lv_label_create(n);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    return n;
}

static void confirm_close(lv_event_t *e)
{
    lv_obj_t *overlay = lv_event_get_user_data(e);
    lv_obj_del_async(overlay);
}

void ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user)
{
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    plain(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, UI_WHITE, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);

    lv_obj_t *dlg = lv_obj_create(overlay);
    plain(dlg);
    lv_obj_set_size(dlg, 260, 110);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, UI_WHITE, 0);
    lv_obj_set_style_border_width(dlg, 2, 0);
    lv_obj_set_style_border_color(dlg, UI_BLACK, 0);

    lv_obj_t *l = lv_label_create(dlg);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *no = ui_button(dlg, btn_no, confirm_close, overlay);
    lv_obj_set_width(no, 110);
    lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 12, -12);

    lv_obj_t *yes = ui_button(dlg, btn_yes, yes_cb, user);
    lv_obj_set_width(yes, 110);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
}
