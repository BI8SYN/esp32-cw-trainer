#ifndef UI_KEYER_H
#define UI_KEYER_H
#include "lvgl.h"
lv_obj_t *ui_keyer_create(void);

/*
 * 触摸电键区的高度。**所有铺电键的页面共用这一个值**——电键页和联网对练页
 * 原来各写各的（108 / 84），同一双手在两页之间切换，按的位置却不一样。
 *
 * 84 是照着对练页的纵向预算定的：240 = 顶栏 28 + 收报 100 + 状态带 28 + 电键 84。
 * 收报区要三行，只能从电键这里让。84 仍然远超 44px 的触控下限，而且每块有
 * 160px 宽，压这 24px 不影响按。
 */
#define UI_PADDLE_H 84

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
