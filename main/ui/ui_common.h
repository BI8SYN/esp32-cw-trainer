#ifndef UI_COMMON_H
#define UI_COMMON_H
#include "lvgl.h"

/* 设计 token（颜色 / 字体 / 尺寸）全部搬到 ui_theme.h。这里 include 它，
   所有已经写 #include "ui_common.h" 的调用点不用改。 */
#include "ui_theme.h"

typedef lv_obj_t *(*ui_screen_builder_t)(void);

/* 把 LVGL 默认主题给对象加的圆角/阴影/内边距/滚动全部清掉，回到一个纯粹的
   矩形。项目里几乎每个自建容器都要这一下，原来是 ui_common.c 里的私有函数，
   ui_home.c 只好把其中两行手抄了一遍。 */
void ui_plain(lv_obj_t *o);

/* 1px 单边分割线。项目从不画整圈描边——分组靠留白，其次才是线，
   所以 13 处 border_side 调用其实都是同一件事。 */
void ui_rule(lv_obj_t *o, lv_border_side_t side, lv_color_t color);

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
