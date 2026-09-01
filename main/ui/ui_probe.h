#ifndef UI_PROBE_H
#define UI_PROBE_H
#include "lvgl.h"

/* 素材样机：只在 -DCW_STYLE_PROBE=ON 的构建里编进来，见 main/CMakeLists.txt。 */
lv_obj_t *ui_probe_create(void);

#endif
