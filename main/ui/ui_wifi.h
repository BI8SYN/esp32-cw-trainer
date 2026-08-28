#ifndef UI_WIFI_H
#define UI_WIFI_H
#include "lvgl.h"
#include "ui_common.h"   /* ui_screen_builder_t */
lv_obj_t *ui_wifi_create(void);
/* 设定退出配网页后回到哪一页；不设则回主页。在 ui_wifi_create() 之前调用。 */
void      ui_wifi_set_return(ui_screen_builder_t builder);
#endif
