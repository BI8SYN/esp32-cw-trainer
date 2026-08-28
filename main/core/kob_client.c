#include "kob_client.h"
#include <string.h>
#include "rng.h"

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
    c->last_id_ms = now_ms;
    tx(c, buf, KOB_PKT_LONG, now_ms);
}

void kob_client_init(kob_client_t *c, const char *office, int16_t wire,
                    uint32_t seed)
{
    memset(c, 0, sizeof *c);
    c->state = KOB_ST_IDLE;
    c->wire = wire;
    /*
     * 号段起点。上界压在 2^30 而不是 2^31：seq 是 int32，一整个会话累加上去
     * 还得留出余量，溢出成负数会让 rx 那边的号段比较和丢包计算全部错乱。
     * 下界取 1 而不是 0，是为了让老固件/机器人（从 0 开始数）的号段
     * 一定落在我的号段之外。
     */
    rng_t r;
    rng_seed(&r, seed);
    c->seq_base = (int32_t)(1u + (rng_next(&r) % 0x40000000u));
    c->tx_seq = c->seq_base;
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

    /* CONNECTING 时这是重试，ONLINE 时这是保活。服务器两种情况都认 CON。
       必须排在 ID 信标前面：信标也走 tx()、也会刷新 last_tx_ms，放在后面
       就等于让信标把保活挤掉。 */
    if (elapsed(now_ms, c->last_tx_ms, KOB_KEEPALIVE_MS)) send_con(c, now_ms);

    /* ID 信标。只在 ONLINE 时发——还没拿到 ACK 就报身份，服务器还没把本站
       纳入中继，白发。用 last_id_ms 单独计时，见 KOB_ID_BEACON_MS 的说明。 */
    if (c->state == KOB_ST_ONLINE && elapsed(now_ms, c->last_id_ms, KOB_ID_BEACON_MS))
        send_id(c, now_ms);
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

    /*
     * 自发包（服务器把我发的包回给我）要丢掉：当成对方发报会自听成回声，
     * 并把「当前发报方」错标成自己。
     *
     * 判据是 **id 相同 且 seq 落在我自己发过的号段里**，两个条件缺一不可。
     *
     * 这里原来只比 id：`if (strcmp(p.id, c->office) == 0) return;`。那样写
     * 有两个问题。一是它挡的东西根本不存在——用两个客户端挂在真实服务器
     * （8.138.130.141:7890，wire 10）实测，**服务器只中继给别人，发送方自己
     * 一个字节都收不到**。二是它顺手挡掉了真正要收的东西：呼号和我相同的
     * 对端，它的每一个码包和 ID 包都被当成回声丢掉。而出厂默认呼号是
     * BI8SYN，两台没改过呼号的设备约在同一条 wire 上，双方都显示在线、都能
     * 看到对方的站点 ID，却谁也听不到谁——这正是「wire 10 通联不上，
     * wire 11/12 却正常」的原因：机器人的 ID 是 BOT / ECHO，撞不上。
     *
     * 加上 seq 号段这一条之后，两种情况都成立：真回显（如果哪天换了服务器
     * 实现，真的开始回显）照样被丢掉，而同呼号的对端因为号段是各自随机的、
     * 不重叠，能正常收到。号段的说明见 kob_client.h 里的 seq_base。
     *
     * 同呼号仍然不该被静默吞掉，它该被看见：ui_net 收到与自己同名的站点时
     * 会提示改呼号。
     */
    if (strcmp(p.id, c->office) == 0 &&
        p.seq >= c->seq_base && p.seq <= c->tx_seq) return;

    if (p.n == 0) {
        if (c->on_peer) c->on_peer(p.id, c->user);
        /* 对方刚报过身份，立刻回报一次自己的——否则它要干等我下一次信标
           （最多 10 秒）才知道我在。限速见 KOB_ID_REPLY_MS。 */
        if (elapsed(now_ms, c->last_id_ms, KOB_ID_REPLY_MS)) send_id(c, now_ms);
        return;
    }

    /* 码包同样证明对端还在线上。只把 ID 包当活着的证据，会把一个正在连续
       发报的站点判成失联——那是最不该判错的时候。 */
    if (c->on_peer) c->on_peer(p.id, c->user);

    /* 换发报方就重置计数基准：seq 是各发各的。 */
    bool same_sender = c->have_rx_seq && strcmp(p.id, c->last_rx_from) == 0;

    /* UDP 会重传/乱序，同一个 seq 收到两次就会把同一个字符播两遍。 */
    if (same_sender && p.seq == c->last_rx_seq) return;

    /* seq 跳变 = 中间丢了包。协议一个字符一个包、UDP 无重传，丢了就是丢了
       一个字，界面要如实显示 ? 而不是假装完整。 */
    int32_t lost = 0;
    if (same_sender && p.seq > c->last_rx_seq) {
        lost = p.seq - c->last_rx_seq - 1;
        if (lost < 0) lost = 0;
        if (lost > KOB_LOST_MAX) lost = KOB_LOST_MAX;
    }

    c->last_rx_seq = p.seq;
    c->have_rx_seq = true;
    {
        size_t n = strlen(p.id);
        if (n > KOB_ID_MAX) n = KOB_ID_MAX;
        memcpy(c->last_rx_from, p.id, n);
        c->last_rx_from[n] = '\0';
    }

    if (c->on_code) c->on_code(p.id, p.code, p.n, p.txt, lost, c->user);
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
