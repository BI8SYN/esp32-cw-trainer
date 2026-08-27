#include "kob_client.h"
#include <string.h>

/*
 * 时间比较一律走这个函数。now 和 then 都是 uint32 毫秒，49.7 天回绕一次。
 * 无符号减法在回绕处依然给出正确的差值（模 2^32），前提是真实间隔小于
 * 约 24.85 天——对保活和超时（秒级）永远成立。
 *
 * 反例：写成 (int32_t)(now - then) >= X 或者把两者提升到 int64 再相减，
 * 都会在回绕点把「过去了 4 秒」算成「过去了 49 天」，把在线连接直接掐掉。
 */
static bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (uint32_t)(now - then) >= ms;
}

static void set_state(kob_client_t *c, kob_state_t st)
{
    if (c->state == st) return;
    c->state = st;
    if (c->on_state) c->on_state(st, c->user);
}

static void tx(kob_client_t *c, const uint8_t *buf, size_t len, uint32_t now_ms)
{
    c->last_tx_ms = now_ms;
    if (c->send) c->send(buf, len, c->user);
}

static void send_con(kob_client_t *c, uint32_t now_ms)
{
    uint8_t buf[KOB_PKT_SHORT];
    kob_pkt_short(buf, KOB_CMD_CON, c->wire);
    tx(c, buf, KOB_PKT_SHORT, now_ms);
}

/* 注册包。见 kob_client.h 顶部：收到 ACK 之后必须发这个，否则服务器不把
   本站纳入中继，且不给任何错误提示。 */
static void send_id(kob_client_t *c, uint32_t now_ms)
{
    uint8_t buf[KOB_PKT_LONG];
    kob_pkt_id(buf, c->office, c->tx_seq, KOB_VERSION_STR);
    tx(c, buf, KOB_PKT_LONG, now_ms);
}

void kob_client_init(kob_client_t *c, const char *office, int16_t wire)
{
    memset(c, 0, sizeof *c);
    c->state = KOB_ST_IDLE;
    c->wire = wire;
    if (office) {
        size_t n = strlen(office);
        if (n > KOB_ID_MAX) n = KOB_ID_MAX;
        memcpy(c->office, office, n);
        c->office[n] = '\0';
    }
}

void kob_client_start(kob_client_t *c, uint32_t now_ms)
{
    c->last_rx_ms = now_ms;
    c->have_rx_seq = false;
    set_state(c, KOB_ST_CONNECTING);
    send_con(c, now_ms);
}

void kob_client_stop(kob_client_t *c, uint32_t now_ms)
{
    if (c->state != KOB_ST_IDLE) {
        uint8_t buf[KOB_PKT_SHORT];
        kob_pkt_short(buf, KOB_CMD_DIS, c->wire);
        tx(c, buf, KOB_PKT_SHORT, now_ms);
    }
    set_state(c, KOB_ST_IDLE);
}

void kob_client_tick(kob_client_t *c, uint32_t now_ms)
{
    if (c->state == KOB_ST_IDLE) return;

    if (c->state == KOB_ST_ONLINE && elapsed(now_ms, c->last_rx_ms, KOB_TIMEOUT_MS)) {
        /* 长时间没有任何来包。退回 CONNECTING 而不是 IDLE——用户没有要求
           断开，我们要继续重连。重连成功后会重新走一遍 ID 注册。 */
        set_state(c, KOB_ST_CONNECTING);
    }

    /* CONNECTING 时这是重试，ONLINE 时这是保活。服务器两种情况都认 CON。 */
    if (elapsed(now_ms, c->last_tx_ms, KOB_KEEPALIVE_MS)) send_con(c, now_ms);
}

void kob_client_rx(kob_client_t *c, const uint8_t *buf, size_t len, uint32_t now_ms)
{
    if (c->state == KOB_ST_IDLE) return;

    kob_packet_t p;
    if (!kob_pkt_parse(buf, len, &p)) return;   /* 长度不合法的包直接丢，不改状态 */

    c->last_rx_ms = now_ms;

    if (p.cmd == KOB_CMD_ACK) {
        if (c->state != KOB_ST_ONLINE) {
            send_id(c, now_ms);                 /* 必须，见头文件说明 */
            set_state(c, KOB_ST_ONLINE);
        }
        return;
    }

    if (p.cmd != KOB_CMD_DAT) return;

    /* 服务器会把我自己发的包原样回给我。当成对方发报会造成自听回声，
       并把「当前发报方」错标成自己。 */
    if (strcmp(p.id, c->office) == 0) return;

    if (p.n == 0) {
        if (c->on_peer) c->on_peer(p.id, c->user);
        return;
    }

    /* UDP 会重传/乱序，同一个 seq 收到两次就会把同一个字符播两遍。 */
    if (c->have_rx_seq && p.seq == c->last_rx_seq) return;
    c->last_rx_seq = p.seq;
    c->have_rx_seq = true;

    if (c->on_code) c->on_code(p.id, p.code, p.n, p.txt, c->user);
}

bool kob_client_send_code(kob_client_t *c, const int32_t *code, int32_t n,
                          const char *txt, uint32_t now_ms)
{
    if (c->state != KOB_ST_ONLINE) return false;
    c->tx_seq++;
    uint8_t buf[KOB_PKT_LONG];
    kob_pkt_code(buf, c->office, c->tx_seq, code, n, txt);
    tx(c, buf, KOB_PKT_LONG, now_ms);
    return true;
}
