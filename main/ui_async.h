#ifndef UI_ASYNC_H
#define UI_ASYNC_H
typedef void (*ui_async_fn_t)(void *arg);
/** 从任意任务/定时器投递到 LVGL 主循环执行。 */
void ui_async_post(ui_async_fn_t fn, void *arg);
#endif
