#ifndef KEY_INPUT_H
#define KEY_INPUT_H
#include <stdbool.h>
#include "esp_err.h"

/*
 * 解码字符回调总是在 key_input 自己的 5ms 任务上下文里同步触发——不是调用
 * key_input_start() / key_input_touch() 的那个任务，也不是 LVGL 所在的任务。
 * LVGL 不是线程安全的：把这个回调接到屏幕上的代码（后续任务）必须把收到的
 * 字符通过 ui_async_post() 转发到 LVGL 主循环里再去碰任何 lv_obj_t，不能在
 * 回调里直接调用 LVGL API，否则会造成偶发、极难复现的界面损坏。
 * c == ' ' 表示词间隔，不是某个具体字母/数字被解码出来。
 */
typedef void (*key_input_char_cb_t)(char c, void *user);

esp_err_t   key_input_init(void);                 /* 创建 5ms tick 任务（初始为停止状态） */
/* 按当前设置（输入源、电键类型、对调、WPM）重新配置并启动；退出电键页时 stop */
void        key_input_start(key_input_char_cb_t cb, void *user);
void        key_input_stop(void);
/* 触摸桨调用：paddle 0 = 左块，1 = 右块（未考虑对调，模块内部处理） */
void        key_input_touch(int paddle, bool pressed);
/* 当前未完成字符的点划串（UI 轮询显示） */
const char *key_input_pattern(void);
bool        key_input_key_down(void);             /* 当前是否在发音（UI 高亮） */

#endif
