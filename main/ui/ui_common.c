#include "ui_common.h"

/* 默认主题除了 grow（已在 make_btn/home 卡片清零 transform_width/height）以外，还
   无条件给每个 lv_btn_class 挂了两份 transition：
   - transition_normal，挂在 selector = LV_STATE_PRESSED，负责按下瞬间的渐变；
   - transition_delayed，挂在 selector = 0（默认态，任何方向的迁移都会扫到它），
     负责松开后延迟一小段再渐变回默认态。
   两份都覆盖 BG_COLOR/BG_OPA/COLOR_FILTER_*（按下态的变暗滤镜）等一串属性，只要
   这些属性在两个状态间取值不同（我们的按下态背景色就是不同的），就会渐变而不是
   瞬间切换——这仍然是 phase 1 禁止的动画。
   `_lv_obj_style_create_transition` 是按“本对象样式数组里第一个把这个属性列进
   transition 描述符的条目”来决定用谁的时长（本地样式永远排在主题样式前面，
   见 lv_obj_add_style 的插入逻辑），所以只要在同样的两个 selector（0 和
   LV_STATE_PRESSED）上，用本地样式挂一份覆盖同一组属性、但 time=0 的描述符，
   就能连着按下和松开两个方向一起顶掉主题那两份、变成瞬时切换（不是删掉整个
   transition 属性——设成 NULL 会在 lv_obj_set_state 内部被当成非空描述符解引用，
   直接崩溃；0 时长的合法描述符则被 lv_map 的边界分支直接返回终值，没有除零，
   已用设备实测确认过）。 */
static const lv_style_prop_t s_no_fade_props[] = {
    LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR,
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT,
    LV_STYLE_TRANSLATE_Y, LV_STYLE_TRANSLATE_X,
    LV_STYLE_TRANSFORM_ZOOM, LV_STYLE_TRANSFORM_ANGLE,
    LV_STYLE_COLOR_FILTER_OPA, LV_STYLE_COLOR_FILTER_DSC,
    0
};

static const lv_style_transition_dsc_t *no_fade_transition(void)
{
    static lv_style_transition_dsc_t dsc;
    static bool inited;
    if (!inited) {
        lv_style_transition_dsc_init(&dsc, s_no_fade_props, lv_anim_path_linear, 0, 0, NULL);
        inited = true;
    }
    return &dsc;
}

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
        /* 必须用 UI_FONT_CN（行高 26），不能用 UI_FONT_MID（行高 37）：
           对象只有 UI_TOPBAR_H=28 高，比 37 矮，LVGL 会把内容当成溢出——
           既会把文字往下偏移，又会给这个 label 打开滚动，箭头因此显得偏低、
           还能被垂直拖动。26 < 28，装得下，两个症状都消失。 */
        lv_obj_set_style_text_font(back, UI_FONT_CN, 0);
        lv_obj_set_size(back, 36, UI_TOPBAR_H);
        lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_left(back, 6, 0);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        /* 电阻屏漏点比电容屏更常见，返回箭头在每一页都存在；扩大到短边 >=48px
           （视觉上的 36x28 不变，只是可点区域向外扩） */
        lv_obj_set_ext_click_area(back, 14);
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
    /* 默认主题（CONFIG_LV_THEME_DEFAULT_GROW）给按下状态加了 transform_width/height
       放大 + 80ms 缓动动画；phase 1 禁止任何动画，这里在本地样式里显式清零覆盖，
       不依赖 Kconfig（谁都可能把那个选项改回去） */
    lv_obj_set_style_transform_width(b, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(b, 0, LV_STATE_PRESSED);
    /* 同一件事的另一半：顶掉主题按下/松开两份 transition，见文件顶部注释 */
    lv_obj_set_style_transition(b, no_fade_transition(), LV_STATE_PRESSED);
    lv_obj_set_style_transition(b, no_fade_transition(), 0);
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
    lv_obj_set_size(n, LV_PCT(100), UI_NOTICE_H);
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
