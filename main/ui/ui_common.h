#ifndef UI_COMMON_H
#define UI_COMMON_H
#include "lvgl.h"

LV_FONT_DECLARE(font20);
LV_FONT_DECLARE(font28);
LV_FONT_DECLARE(EN_BIG);
#define UI_FONT_CN   (&font20)
#define UI_FONT_MID  (&font28)
#define UI_FONT_BIG  (&EN_BIG)
/*
 * 顶栏状态用的小号数字（行高 17）。项目自带的三个字体行高分别是 26/37/82，
 * 在 28px 的顶栏里和 16px 的图标并排都显得头重脚轻。
 *
 * 用 14 而不是更小的 12：components/lvgl/lv_conf.h 里虽然把 8/10/12/16 都
 * 置 1 了，但实际编译进来的只有 14（12 会报 undeclared），说明那份 lv_conf
 * 没有生效、走的是 LVGL 默认配置。14 是唯一确定可用的。
 *
 * 只有 ASCII，不能拿去显示中文。
 */
#define UI_FONT_TINY (&lv_font_montserrat_14)
#define UI_TOPBAR_H  28
/*
 * 提示条高度。必须 >= UI_FONT_CN 的行高 26——原来写的是 22，比行高还矮，
 * 文字上下各被裁掉 2px。这个坑在本项目里反复出现（配网页输入框、对练页
 * 状态带、电键页第二行），共享组件里修一次，所有调用方都受益。
 *
 * 调用方在 ui_notice() 之后把 top 加上这个值，不要再写死 22。
 */
#define UI_NOTICE_H  28
#define UI_GRAY_BG   lv_color_hex(0xEEEEEE)
#define UI_GRAY_DOWN lv_color_hex(0x999999)
#define UI_BLACK     lv_color_hex(0x000000)
#define UI_WHITE     lv_color_hex(0xFFFFFF)

typedef lv_obj_t *(*ui_screen_builder_t)(void);

void      ui_nav_show(ui_screen_builder_t builder);
lv_obj_t *ui_screen_create(void);
/* 返回的顶栏对象可以继续往右侧加控件（主页的齿轮/电量就是这样接上去的）。
   往右加的控件必须保持右对齐；如果哪天有页面同时传 back_cb 又在右侧加控件，
   要重新检查返回箭头和右侧控件各自放大过的可点区域（ext_click_area）会不会
   彼此重叠——目前两者从未在同一页出现，这个检查还没做过。 */
lv_obj_t *ui_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user);
lv_obj_t *ui_cell(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_event_cb_t cb, void *user);
lv_obj_t *ui_notice(lv_obj_t *parent, const char *text);
void      ui_confirm(const char *text, const char *btn_no, const char *btn_yes, lv_event_cb_t yes_cb, void *user);

#endif
