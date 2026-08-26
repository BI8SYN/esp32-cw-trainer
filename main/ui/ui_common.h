#ifndef UI_COMMON_H
#define UI_COMMON_H
#include "lvgl.h"

LV_FONT_DECLARE(font20);
LV_FONT_DECLARE(font28);
LV_FONT_DECLARE(EN_BIG);
#define UI_FONT_CN   (&font20)
#define UI_FONT_MID  (&font28)
#define UI_FONT_BIG  (&EN_BIG)
#define UI_TOPBAR_H  28
#define UI_GRAY_BG   lv_color_hex(0xEEEEEE)
#define UI_GRAY_DOWN lv_color_hex(0x999999)
#define UI_BLACK     lv_color_hex(0x000000)
#define UI_WHITE     lv_color_hex(0xFFFFFF)

typedef lv_obj_t *(*ui_screen_builder_t)(void);

void      ui_nav_show(ui_screen_builder_t builder);
lv_obj_t *ui_screen_create(void);
lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user);
lv_obj_t *ui_notice(lv_obj_t *parent, const char *text);
void      ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user);

#endif
