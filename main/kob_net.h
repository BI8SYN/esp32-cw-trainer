#ifndef KOB_NET_H
#define KOB_NET_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "kob_packet.h"

/*
 * 联网对练的运行时：一个 UDP socket 任务，驱动 kob_client（协议状态机）与
 * kob_rxbuf（收报缓冲），把收到的时序接到侧音，把本机电键的时序打包发出。
 *
 * 两个回调都在 socket 任务里触发，**不是** LVGL 任务。UI 必须用
 * ui_async_post() 转发，不能在回调里碰 lv_obj_t。
 */

typedef enum {
    KOB_NET_OFF,
    KOB_NET_WIFI,            /* 正在连 WiFi */
    KOB_NET_CONNECTING,      /* WiFi 好了，正在接入 wire */
    KOB_NET_ONLINE,
    KOB_NET_ERR_WIFI,        /* WiFi 连不上——与下面那条要分开显示 */
    KOB_NET_ERR_SERVER,      /* WiFi 好了但服务器不可达 */
    KOB_NET_RECONNECTING,
} kob_net_state_t;

#define KOB_NET_PEER_MAX 4

typedef struct {
    kob_net_state_t state;
    char            wifi_ssid[33];
    char            peers[KOB_NET_PEER_MAX][KOB_ID_MAX + 1];
    int             peer_n;
    char            sender[KOB_ID_MAX + 1];   /* 当前发报方；空串 = 线路空闲 */
    bool            i_am_sending;
    bool            broken;                   /* 我正在发时被别人打断 */
} kob_net_status_t;

/*
 * 一条收发记录。from 为空串表示是本机发出的。
 *
 * 这里**故意不用回调**。socket 任务如果直接回调进 UI，就得走 ui_async_post()，
 * 而它用 portMAX_DELAY 取 ui_lock()——那把锁在 lv_timer_handler() 全程被持有，
 * 页面删除事件正是在里面跑的。于是 kob_net_stop()（LVGL 线程）等 socket 任务
 * 退出、socket 任务等 ui_lock()，界面至少冻结一秒；更糟的是随后的
 * esp_wifi_deinit() 可能在 socket 任务还活着的时候执行。
 *
 * 改成单生产者（socket 任务）单消费者（UI 轮询）的环形队列后，kob_net 完全
 * 不碰 LVGL 的任何东西，死锁的前提就不存在了。状态本来也有轮询在读，回调
 * 是多余的。
 */
typedef struct {
    char from[24];
    char txt[16];
} kob_net_line_t;

/* 启动：开 WiFi -> 连 wire -> 接管电键。非阻塞，进度靠轮询 kob_net_status()。 */
esp_err_t kob_net_start(void);
/* UI 轮询调用，取走一条记录；没有新记录时返回 false。 */
bool      kob_net_pop_line(kob_net_line_t *out);
/* 停止：发 DIS、关 socket、摘掉电键回调、关 WiFi。 */
void      kob_net_stop(void);
/* 换 wire：停掉 socket 任务、按 net_store 里的新 wire 重开一遍，**不动 WiFi**。
   调用前先把 net_store_get()->wire 改好。 */
esp_err_t kob_net_rewire(void);

const kob_net_status_t *kob_net_status(void);

#endif
