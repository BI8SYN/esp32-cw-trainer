#ifndef KEY_INPUT_H
#define KEY_INPUT_H
#include <stdbool.h>
#include <stdint.h>
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

/*
 * 键控边沿回调：每次侧音开/关翻转时同步触发，t_ms 是翻转发生的时刻。
 * 联网对练要把本机的发报手法编码成 CWCom 的毫秒时序数组，需要的正是这些
 * 边沿时刻——字符回调（上面那个）给的是解码结果，丢掉了时长信息。
 *
 * 与字符回调同一个上下文：key_input 自己的 5ms 任务（core 1，优先级 22），
 * 不是 LVGL 任务。回调里绝不能碰 lv_obj_t，也不要做耗时操作——它跑在
 * 整个系统优先级第二高的任务上，阻塞它会直接毁掉发报时序。
 */
typedef void (*key_input_edge_cb_t)(bool down, uint32_t t_ms, void *user);

/*
 * 触摸桨与外接口是**并联**的：两路谁按都算，没有"输入源"这种二选一设置。
 * 合并规则见 core/key_merge.h（纯函数，有单测）。
 *
 * 外接口因此是**永远在采样**的，这带来一个必须处理的问题：一根空闲电平就是低的
 * 线会把电键彻底钉死。这不是罕见故障——本机 GPIO27 上接了蜂鸣器是一例，而成品
 * 直键的 TS 单声道插头插进 3.5mm 立体声母座时，套筒会把 Ring 和 Sleeve 短接，
 * 划线恒为低，**每插一次直键都会发生**。
 *
 * 门闩（armed）：**一根线必须先被观察到"松开"过一次，才被允许说话。**
 * key_input_start() 里原始采样一次决定初值，5ms 任务里每读到一次去抖后的"松开"
 * 就重新置位。没开闸的线一律按没按下处理，而且只掐外接那一路——同侧的触摸块
 * 照常可用，一根坏线不会让整台机器没法用。
 */
esp_err_t   key_input_init(void);                 /* 创建 5ms tick 任务（初始为停止状态） */

/*
 * 按当前设置（电键类型、对调、WPM）重新配置并启动。
 *
 * user 同时是**归属令牌**：谁 start 的谁拥有电键，见下面 key_input_stop_if()。
 * start 还会清掉边沿回调——接管电键的人从一张白纸开始，要边沿就在 start
 * 之后自己再挂一次。
 */
void        key_input_start(key_input_char_cb_t cb, void *user);

/*
 * 只在当前归属 owner 时才停止，返回是否真的停了。
 *
 * **页面的 on_delete 必须用这个，不能用 key_input_stop()。** 原因和
 * cw_player_stop_if() 完全一样：ui_nav_show() 会先建好新页面（新页面此时
 * 可能已经 key_input_start() 了），再异步删除旧页面。旧页面的 on_delete
 * 若无条件停止，掐掉的是新页面刚启动的电键——表现为进手法页按电键没声音、
 * 返回电键页也没声音，退到主页再进才恢复（主页不碰 key_input，没人掐它）。
 */
bool        key_input_stop_if(void *owner);

/* 无条件停止。目前没有页面该用它——页面一律走 key_input_stop_if()。
   保留作为"不管归属、硬停"的原语。 */
void        key_input_stop(void);
/* 触摸桨调用：paddle 0 = 左块，1 = 右块（未考虑对调，模块内部处理） */
void        key_input_touch(int paddle, bool pressed);
/* 可在任意时刻设置/清除，cb 传 NULL 表示不再需要边沿。 */
void        key_input_set_edge_cb(key_input_edge_cb_t cb, void *user);
/* 当前未完成字符的点划串（UI 轮询显示） */
const char *key_input_pattern(void);
bool        key_input_key_down(void);             /* 当前是否在发音（侧音状态） */

/*
 * 屏幕上第 paddle 块（0=左，1=右）当前是否按下——触摸与外接合并后、左右对调
 * **之前**。电键块的高亮用它：这样按外接电键时屏幕上对应的那一块也会亮，而且
 * 开了对调也不会变成按左块高亮右块。
 */
bool        key_input_paddle_down(int paddle);

/*
 * 哪几根外接线正被门闩挡在外面：bit0 = 点线，bit1 = 划线。非 0 意味着那根线在
 * 启动时就读成"按下"且此后没松开过。UI 拿它给出提示——静默忽略一根坏线，表现
 * 是"按了没反应且毫无线索"，那正是 GPIO27 那次最难查的地方。
 */
uint8_t     key_input_ext_blocked(void);

#endif
