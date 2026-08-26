#ifndef CW_PLAYER_H
#define CW_PLAYER_H
#include <stdbool.h>
#include "esp_err.h"
#include "morse_timing.h"

typedef enum { CW_PLAYER_EV_CHAR_START, CW_PLAYER_EV_DONE, CW_PLAYER_EV_STOPPED } cw_player_event_t;

/*
 * 回调触发的上下文并不统一，接入 LVGL 等非线程安全 UI 时必须注意：
 *   - CW_PLAYER_EV_CHAR_START / CW_PLAYER_EV_DONE：总是在 esp_timer 服务任务里触发，
 *     不是调用 cw_player_play() 的那个任务。
 *   - CW_PLAYER_EV_STOPPED：同步地在调用 cw_player_stop() 的那个任务里触发——可能是
 *     esp_timer 任务，也可能是 UI 任务，也可能是别的任何任务，取决于谁调用了 stop()。
 * 三种事件都不能假定与 LVGL 所在任务相同，必须统一走同一条线程安全路径（消息队列/
 * 锁）转发给 UI，不能只处理来自 esp_timer 任务的那两种而遗漏 STOPPED。
 *
 * 三种事件触发时都不持有本模块内部的互斥锁，因此可以在回调里同步调用
 * cw_player_play() / cw_player_stop()（不会自锁死）；但上面所说的任务上下文
 * 差异依然存在，UI 代码仍需通过统一的线程安全路径转发，不能直接操作 LVGL。
 */
typedef void (*cw_player_cb_t)(cw_player_event_t ev, char c, void *user);

esp_err_t cw_player_init(void);
void      cw_player_set_timing(const morse_timing_t *t);
esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user);
/* 目前没有调用方：UI 侧全部改用 cw_player_stop_if()（见下）。保留作为无条件
 * 停止的公开原语，供以后需要"不管归属、硬停止全局播放"的场景使用。 */
void      cw_player_stop(void);
/* 只在当前播放会话属于 user 时才停止，否则什么都不做，返回是否真的停了。
 * 页面的 on_delete 必须用这个而不是 cw_player_stop()：ui_nav_show 会先建好新页面
 * （新页面此时可能已经开始播放），再异步删除旧页面。旧页面的 on_delete 若无条件
 * 停止播放，掐掉的是新页面刚开始的音频。 */
bool      cw_player_stop_if(void *user);

#endif
