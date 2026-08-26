#include "ui_async.h"
#include <stdlib.h>
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ui_async";

typedef struct { ui_async_fn_t fn; void *arg; } msg_t;

static void trampoline(void *p)
{
    msg_t *m = p;
    m->fn(m->arg);
    free(m);
}

/* 保护 LVGL 内部状态（堆自由链表 + 定时器链表）不被 lv_timer_handler() 所在的
 * 单线程假设之外的并发访问破坏。lv_async_call()（内部靠 lv_mem_alloc + 不加锁的
 * lv_timer_create 把新节点挂进全局定时器链表）和 lv_timer_handler() 本身
 * 是仅有的两处触碰这份共享状态的地方——见 main.c 主循环与本文件 ui_async_post()
 * 里各自的加锁点。 */
static SemaphoreHandle_t s_lock;

void ui_async_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
}

void ui_lock(void)
{
    /* 必须是递归锁：main.c 在这把锁下运行 lv_timer_handler()，而 LVGL 定时器/
     * 事件回调（也就是 main/ui/ 下几乎所有代码）在同一个任务、同一次调用栈里
     * 都可能再调用 ui_async_post() —— 本函数会在同一个任务上被再取一次锁。
     * 用非递归锁会在这里直接自锁死；递归锁允许同一任务反复取锁，只挡真正
     * 不同的任务（例如 core 1 上的 key_input 任务、esp_timer 服务任务）。 */
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

void ui_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

void ui_async_post(ui_async_fn_t fn, void *arg)
{
    msg_t *m = malloc(sizeof *m);
    if (!m) {
        ESP_LOGW(TAG, "post dropped: malloc failed");
        return;
    }
    m->fn = fn; m->arg = arg;

    /* lv_async_call() 内部用 LVGL 自己的、未加锁的 TLSF 堆分配一个 lv_timer_t
     * 并把它插入全局定时器链表；main.c 的 lv_timer_handler() 会遍历/重新排列
     * 同一份链表并从同一份堆里分配/释放。本函数会从 core 1 的电键任务、
     * esp_timer 服务任务（cw_player 的回调）等与 LVGL 主循环（core 0）真正
     * 并发的任务里调用——这是双核 SMP，不是"看起来会跑在同一个核上所以大概率
     * 没事"。不加锁的话，两侧同时改写同一份自由链表/定时器链表会破坏结构，
     * 表现为偶发性的莫名其妙的复位。ui_lock()/ui_unlock() 把这唯一的调用点
     * 和 main.c 里的 lv_timer_handler() 互斥起来即可覆盖所有生产者。 */
    ui_lock();
    lv_res_t res = lv_async_call(trampoline, m);
    ui_unlock();

    if (res != LV_RES_OK) {
        ESP_LOGW(TAG, "post dropped: lv_async_call failed");
        free(m);
    }
}
