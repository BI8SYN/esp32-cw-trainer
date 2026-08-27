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

typedef void (*kob_net_evt_cb_t)(const kob_net_status_t *st, void *user);
/* 一条完整的收发记录。from 为 NULL 表示是本机发出的。 */
typedef void (*kob_net_text_cb_t)(const char *from, const char *txt, void *user);

/* 启动：开 WiFi -> 连 wire -> 接管电键。整个过程非阻塞，进度走 evt 回调。 */
esp_err_t kob_net_start(kob_net_evt_cb_t evt, kob_net_text_cb_t text, void *user);
/* 停止：发 DIS、关 socket、摘掉电键回调、关 WiFi。 */
void      kob_net_stop(void);

const kob_net_status_t *kob_net_status(void);

#endif
