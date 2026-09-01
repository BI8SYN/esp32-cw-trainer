#ifndef PROBE_ASSETS_H
#define PROBE_ASSETS_H
#include "lvgl.h"

/* 由 tools/gen_probe_assets.py 生成，见 probe_assets.c 顶部说明。
   _flat 是直接量化（复现 LVGL 实时渐变的色带），_dith 是
   Floyd-Steinberg 抖动后再量化，两者并排就是样机要看的那个对比。 */
extern const lv_img_dsc_t probe_grad_paper_flat;
extern const lv_img_dsc_t probe_grad_paper_dith;
extern const lv_img_dsc_t probe_grad_copper_flat;
extern const lv_img_dsc_t probe_grad_copper_dith;

/* 纸纹，32x32 可平铺。噪声强度 sigma = 1 / 2 / 4 / 7（8 位灰阶） */
extern const lv_img_dsc_t probe_noise0;
extern const lv_img_dsc_t probe_noise1;
extern const lv_img_dsc_t probe_noise2;
extern const lv_img_dsc_t probe_noise3;

#endif
