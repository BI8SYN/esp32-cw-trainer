#ifndef UI_ASSETS_H
#define UI_ASSETS_H
#include "lvgl.h"

/* 由 tools/gen_ui_assets.py 生成，见该脚本的说明。
   都是 32x32 可平铺的小图，配 lv_obj_set_style_bg_img_tiled(o, true, 0) 使用
   ——v8 里唯一的平铺方式，2KB 的素材能铺满任意面积。 */
extern const lv_img_dsc_t ui_grain_paper;  /* 纸纤维，页面底纹 */
extern const lv_img_dsc_t ui_grain_brass;  /* 黄铜拉丝，电键桨 */

#endif
