#ifndef UI_ICONS_H
#define UI_ICONS_H
#include "lvgl.h"

/*
 * 顶栏图标，全部用 LVGL 图元（矩形/线）现画。
 *
 * 为什么不用字体图标：本项目的中文字体 font20 的码点范围是 0x09~0xffe5，
 * **不含 LVGL 符号所在的 0xF000+ 私有区**（实测 grep "unicode = 0xF" 返回
 * 0 条），LV_SYMBOL_* 一律渲染成豆腐块——配网页的键盘就是栽在这上面。
 *
 * 为什么不搬 fmo-companion 的 ui_icons.c：那是 186KB 的多色深 alpha 位图，
 * 在正为 OTA 省空间的时候加进来不划算，而且它的视觉语言和本项目刻意的
 * 扁平灰阶不搭。图元画法零字节开销，风格也统一。
 *
 * 每个函数都在 parent 上创建一组子对象并返回容器，调用方负责定位。
 */

/* WiFi：三格递增的竖条。level 0~3；level < 0 表示未连接（全部空心）。 */
lv_obj_t *ui_icon_wifi(lv_obj_t *parent);
void      ui_icon_wifi_set(lv_obj_t *icon, int level);

/* 电池：外框 + 正极凸点 + 内部填充条。percent 0~100。 */
lv_obj_t *ui_icon_battery(lv_obj_t *parent);
void      ui_icon_battery_set(lv_obj_t *icon, uint8_t percent);

/* 设置：齿轮（中心方块 + 四周四个小块）。 */
lv_obj_t *ui_icon_gear(lv_obj_t *parent);

#endif
