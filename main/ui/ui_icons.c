#include "ui_icons.h"
#include "ui_common.h"

/* 顶栏高 28px，图标统一 16px 高，上下各留 6px */
#define ICON_H 16

static lv_obj_t *box(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void fill(lv_obj_t *o, bool solid)
{
    if (solid) {
        lv_obj_set_style_bg_color(o, UI_BLACK, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 0, 0);
    } else {
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_color(o, UI_GRAY_DOWN, 0);
    }
}

/* ---------------------------------------------------------------- WiFi */

/* 三格竖条，高度递增。用竖条而不是同心弧：LVGL 画弧要 lv_arc，带一整套
   自己的样式和动画；三个矩形在 16px 见方里反而更清楚。 */
lv_obj_t *ui_icon_wifi(lv_obj_t *parent)
{
    lv_obj_t *c = box(parent, 16, ICON_H);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = box(c, 4, 5 + i * 5);
        lv_obj_set_pos(b, i * 5, ICON_H - (5 + i * 5));
        fill(b, false);
    }
    return c;
}

void ui_icon_wifi_set(lv_obj_t *icon, int level)
{
    if (!icon) return;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_obj_get_child(icon, i);
        if (b) fill(b, level > i);
    }
}

/* ---------------------------------------------------------------- 电池 */

lv_obj_t *ui_icon_battery(lv_obj_t *parent)
{
    lv_obj_t *c = box(parent, 20, ICON_H);

    lv_obj_t *shell = box(c, 17, 10);          /* 外框 */
    lv_obj_set_pos(shell, 0, 3);
    lv_obj_set_style_bg_opa(shell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shell, 1, 0);
    lv_obj_set_style_border_color(shell, UI_BLACK, 0);

    lv_obj_t *nub = box(c, 2, 5);              /* 正极凸点 */
    lv_obj_set_pos(nub, 17, 6);
    fill(nub, true);

    lv_obj_t *bar = box(c, 1, 6);              /* 电量填充，宽度随百分比变 */
    lv_obj_set_pos(bar, 2, 5);
    fill(bar, true);

    return c;
}

void ui_icon_battery_set(lv_obj_t *icon, uint8_t percent)
{
    if (!icon) return;
    lv_obj_t *bar = lv_obj_get_child(icon, 2);
    if (!bar) return;
    if (percent > 100) percent = 100;
    /* 外框内可用宽度 13px（17 减去两边框和各 1px 内缩） */
    int w = percent * 13 / 100;
    if (w < 1) w = 1;
    lv_obj_set_width(bar, w);
    /* 低电量只改填充色，不加感叹号——顶栏没有多余的横向空间，
       而且旁边就有百分比数字，够用了。 */
    lv_obj_set_style_bg_color(bar, percent < 15 ? UI_GRAY_DOWN : UI_BLACK, 0);
}
