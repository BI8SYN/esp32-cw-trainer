#ifndef UI_KEYER_H
#define UI_KEYER_H
#include "lvgl.h"
lv_obj_t *ui_keyer_create(void);

/*
 * 触摸双桨：在 parent 上按 (y, h) 铺两块 160 宽的桨，事件直接接到
 * key_input_touch()，创建出的两个对象写进 out[2]。
 *
 * 抽出来是因为联网对练页也要一模一样的一对桨。之前那边自己复制了一份，
 * 字体写成了 UI_FONT_MID——而 font28 只有 96 个 ASCII 字形、没有中文，
 * "点""划"直接渲染成两个豆腐块。共用一份就不会再各自走样。
 */
void ui_keyer_paddles(lv_obj_t *parent, int y, int h, lv_obj_t *out[2]);
#endif
