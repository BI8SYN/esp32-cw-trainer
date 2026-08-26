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
 */
typedef void (*cw_player_cb_t)(cw_player_event_t ev, char c, void *user);

esp_err_t cw_player_init(void);
void      cw_player_set_timing(const morse_timing_t *t);
esp_err_t cw_player_play(const char *text, cw_player_cb_t cb, void *user);
void      cw_player_stop(void);
bool      cw_player_is_playing(void);

#endif
