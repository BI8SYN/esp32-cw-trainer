#ifndef KOB_CLIENT_H
#define KOB_CLIENT_H
#include "kob_packet.h"

/*
 * CWCom wire 客户端的协议状态机。纯 C，不碰 socket——收发字节全部经由回调，
 * 时间由调用方以毫秒喂进来。因此可以在主机上完整测试，不需要网络。
 *
 * 状态迁移：
 *
 *   IDLE --start()--> CONNECTING --收到 ACK--> ONLINE
 *     ^                    ^                     |
 *     |                    +--- tick() 超时 -----+
 *     +--- stop()（发 DIS）------------------------
 *
 * CONNECTING -> ONLINE 的那一步里会**自动补发一个 ID 包**。这一步不能省：
 * 实测确认，只发 CON 拿到 ACK 之后直接发码包，同一条 wire 上的其他客户端
 * 收不到任何东西，而服务器不报错、不拒绝，静默丢弃。漏掉它的表现是
 * 「界面显示在线，但对方永远听不到我」，且没有任何错误可查。
 */

typedef enum {
    KOB_ST_IDLE,
    KOB_ST_CONNECTING,
    KOB_ST_ONLINE,
} kob_state_t;

/* 发送由宿主实现：主机测试里记录到数组，设备上是 sendto()。 */
typedef void (*kob_send_fn)(const uint8_t *buf, size_t len, void *user);
/* 别人发来的码包。code/n 是对端的真实手法时序，txt 是可读文本。 */
typedef void (*kob_code_fn)(const char *from, const int32_t *code, int32_t n,
                            const char *txt, void *user);
/* 别人的 ID 包（站点出现或更新）。 */
typedef void (*kob_peer_fn)(const char *id, void *user);
typedef void (*kob_state_fn)(kob_state_t st, void *user);

typedef struct {
    kob_state_t  state;
    int16_t      wire;
    char         office[KOB_ID_MAX + 1];
    int32_t      tx_seq;
    int32_t      last_rx_seq;
    bool         have_rx_seq;
    uint32_t     last_tx_ms;
    uint32_t     last_rx_ms;
    kob_send_fn  send;
    kob_code_fn  on_code;
    kob_peer_fn  on_peer;
    kob_state_fn on_state;
    void        *user;
} kob_client_t;

#define KOB_KEEPALIVE_MS  10000u
#define KOB_TIMEOUT_MS    30000u
#define KOB_VERSION_STR   "CWTRN 1.0"

void kob_client_init(kob_client_t *c, const char *office, int16_t wire);
void kob_client_start(kob_client_t *c, uint32_t now_ms);
void kob_client_stop(kob_client_t *c, uint32_t now_ms);
/* 周期调用（建议 100~500ms 一次），驱动保活与超时判定。 */
void kob_client_tick(kob_client_t *c, uint32_t now_ms);
void kob_client_rx(kob_client_t *c, const uint8_t *buf, size_t len, uint32_t now_ms);
/* 仅在 ONLINE 时成功；返回是否真的发出去了。 */
bool kob_client_send_code(kob_client_t *c, const int32_t *code, int32_t n,
                          const char *txt, uint32_t now_ms);

#endif
