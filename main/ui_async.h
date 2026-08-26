#ifndef UI_ASYNC_H
#define UI_ASYNC_H
typedef void (*ui_async_fn_t)(void *arg);
/** 从任意任务/定时器投递到 LVGL 主循环执行。 */
void ui_async_post(ui_async_fn_t fn, void *arg);

/* main.c 与 ui_async.c 共用的那把递归互斥锁：main.c 的主循环用它包住
 * lv_timer_handler()，ui_async_post() 用它包住 lv_async_call()。二者是
 * LVGL 状态的唯一并发生产者/消费者对，见 ui_async.c 里 ui_lock() 的注释。
 *
 * 必须在任何可能调用 ui_async_post() 的任务启动之前调用一次 ui_async_init()——
 * 目前在 app_main() 最开头调用，早于 key_input_init()/cw_player_init() 等。 */
void ui_async_init(void);
void ui_lock(void);
void ui_unlock(void);
#endif
