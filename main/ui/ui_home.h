#ifndef UI_HOME_H
#define UI_HOME_H
#include "lvgl.h"
#include <stdint.h>
lv_obj_t *ui_home_create(void);
void      ui_home_set_battery(uint8_t percent);
#endif
