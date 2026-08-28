#include "test_util.h"
#include "kob_client.h"

static uint8_t     g_sent[16][KOB_PKT_LONG];
static size_t      g_sent_len[16];
static int         g_sent_n;
static char        g_code_from[64];
static int32_t     g_code_n;
static char        g_code_txt[64];
static char        g_peer[64];
static kob_state_t g_state;

static void rec_send(const uint8_t *b, size_t l, void *u)
{
    (void)u;
    if (g_sent_n < 16) {
        memcpy(g_sent[g_sent_n], b, l);
        g_sent_len[g_sent_n] = l;
        g_sent_n++;
    }
}

static int32_t g_lost;

static void rec_code(const char *f, const int32_t *c, int32_t n, const char *t,
                     int32_t lost, void *u)
{
    (void)c; (void)u;
    snprintf(g_code_from, sizeof g_code_from, "%s", f);
    g_code_n = n;
    g_lost = lost;
    snprintf(g_code_txt, sizeof g_code_txt, "%s", t);
}

static void rec_peer(const char *id, void *u)
{
    (void)u;
    snprintf(g_peer, sizeof g_peer, "%s", id);
}

static void rec_state(kob_state_t s, void *u) { (void)u; g_state = s; }

static void setup(kob_client_t *c)
{
    g_sent_n = 0; g_code_n = -1; g_lost = -1;
    g_code_from[0] = 0; g_code_txt[0] = 0; g_peer[0] = 0;
    g_state = KOB_ST_IDLE;
    kob_client_init(c, "BI8SYN", 11, 0xC0FFEEu);
    c->send = rec_send; c->on_code = rec_code; c->on_peer = rec_peer; c->on_state = rec_state;
}

static const uint8_t ACK[2] = { 0x05, 0x00 };

/* 走到 ONLINE 的标准序列，多数用例都要先走一遍 */
static void bring_online(kob_client_t *c)
{
    kob_client_start(c, 1000);
    kob_client_rx(c, ACK, sizeof ACK, 1200);
}

TEST(start_sends_con_and_enters_connecting) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    ASSERT_EQ_INT(c.state, KOB_ST_CONNECTING);
    ASSERT_EQ_INT(g_sent_n, 1);
    ASSERT_EQ_INT((int)g_sent_len[0], KOB_PKT_SHORT);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[0], g_sent_len[0], &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_CON);
}

TEST(ack_triggers_id_registration_then_online) {
    /* 这是实测踩出来的一条：只发 CON 拿到 ACK 之后直接发码包，同 wire 的
       另一个客户端收不到任何东西，服务器不报错、静默丢弃。必须补发 ID 包。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    ASSERT_EQ_INT(g_sent_n, 2);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[1], g_sent_len[1], &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_DAT);
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_EQ_STR(p.id, "BI8SYN");
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
    ASSERT_EQ_INT(g_state, KOB_ST_ONLINE);
}

TEST(cannot_send_code_before_online) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    const int32_t v[] = { 60, -60, 180 };
    ASSERT_TRUE(!kob_client_send_code(&c, v, 3, "A", 1100));
}

TEST(send_code_increments_seq) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60, -60, 180 };
    ASSERT_TRUE(kob_client_send_code(&c, v, 3, "A", 1300));
    ASSERT_TRUE(kob_client_send_code(&c, v, 3, "N", 1400));
    kob_packet_t a, b;
    ASSERT_TRUE(kob_pkt_parse(g_sent[2], g_sent_len[2], &a));
    ASSERT_TRUE(kob_pkt_parse(g_sent[3], g_sent_len[3], &b));
    ASSERT_TRUE(b.seq > a.seq);
    ASSERT_EQ_STR(a.txt, "A");
    ASSERT_EQ_STR(b.txt, "N");
}

TEST(incoming_code_from_others_is_reported) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { -2000, 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 5, v, 4, "A");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_STR(g_code_from, "BG7XXX");
    ASSERT_EQ_INT(g_code_n, 4);
    ASSERT_EQ_STR(g_code_txt, "A");
}

TEST(own_echo_is_ignored) {
    /* 服务器如果把我发的包回给我（本项目的服务器不会，但换个实现就难说），
       必须丢掉：否则自听成回声，且「当前发报方」被错标成自己。
       判据是 id 相同 **且** seq 在我自己发过的号段里。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60, -60, 180 };
    kob_client_send_code(&c, v, 3, "A", 1400);
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BI8SYN", c.tx_seq, v, 3, "A");   /* 我刚发的那个 seq */
    g_code_n = -1;
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_INT(g_code_n, -1);
}

TEST(own_id_echo_is_ignored) {
    /* ID 包用的是号段起点，边界上不能漏。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_id(pkt, "BI8SYN", c.seq_base, "CWTRN 1.0");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_STR(g_peer, "");
}

TEST(seq_base_is_seeded_and_above_legacy_range) {
    /* 号段起点必须随种子变，否则两台设备又会重叠。上界压在 2^30 以内，
       一整个会话累加上去也不会把 int32 顶成负数。 */
    kob_client_t a, b;
    kob_client_init(&a, "BI8SYN", 11, 1u);
    kob_client_init(&b, "BI8SYN", 11, 2u);
    ASSERT_TRUE(a.seq_base != b.seq_base);
    ASSERT_TRUE(a.seq_base > 0 && a.seq_base <= 0x40000000);
    ASSERT_TRUE(b.seq_base > 0 && b.seq_base <= 0x40000000);
    ASSERT_EQ_INT(a.tx_seq, a.seq_base);
}

TEST(same_callsign_peer_is_delivered) {
    /*
     * 同呼号的对端必须能收到。
     *
     * 这里曾经有一条 `if (strcmp(p.id, c->office) == 0) return;`，
     * 用意是丢掉「服务器回显给我的自发包」。实测（两个客户端同
     * 时挂在 8.138.130.141:7890 wire 10）证明服务器**从不回显发送方
     * 自己的包** —— 发报方一个字节都收不到，只有对端收到。
     *
     * 所以那条过滤只会在一种情况下生效：对端呼号和我相同。而出厂
     * 默认呼号就是 BI8SYN，于是两台没改过呼号的设备上同一条 wire，互相
     * 把对方的每一个包当成自己的回声丢掉：两边都显示在线，谁也听不到谁。
     * 机器人 wire（11/12）不受影响，因为它们的 ID 是 BOT / ECHO。
     */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    /* 对端的号段是它自己随机的，和我的不重叠——这里用 9 代表「我没发过」。 */
    ASSERT_TRUE(c.seq_base > 9);
    kob_pkt_code(pkt, "BI8SYN", 9, v, 3, "A");
    g_code_n = -1;
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_INT(g_code_n, 3);
    ASSERT_EQ_STR(g_code_txt, "A");
}

TEST(same_callsign_peer_id_packet_reports_peer) {
    /* ID 包同理：同呼号的对端上线，应该出现在站点列表里。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    uint8_t pkt[KOB_PKT_LONG];
    ASSERT_TRUE(c.seq_base > 0);
    kob_pkt_id(pkt, "BI8SYN", 0, "CWTRN 1.0");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_STR(g_peer, "BI8SYN");
}

TEST(id_beacon_resent_periodically_while_online) {
    /*
     * ID 包原来只在建连时发一次，于是站点表两头都是错的：晚加入的人看不到
     * 早到的人，已经走了的人也没法过期。信标必须周期重发。
     */
    kob_client_t c; setup(&c);
    bring_online(&c);
    int before = g_sent_n;
    kob_client_tick(&c, 1200 + KOB_ID_BEACON_MS - 1);
    ASSERT_EQ_INT(g_sent_n, before);            /* 还没到点 */

    kob_client_tick(&c, 1200 + KOB_ID_BEACON_MS);
    ASSERT_TRUE(g_sent_n > before);
    /* 这一拍里保活 CON 和 ID 信标都该发出去，最后一个是 ID 包（n == 0）。 */
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[g_sent_n - 1], g_sent_len[g_sent_n - 1], &p));
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_EQ_STR(p.id, "BI8SYN");
}

TEST(id_beacon_not_starved_by_outgoing_code) {
    /*
     * 信标用自己的时间戳，不能蹭 last_tx_ms：正在连续发报的站点 last_tx_ms
     * 一直在刷新，蹭它就永远发不出信标——而那正是最该让别人知道你在的时候。
     */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60 };
    /* 卡在信标到点前一毫秒发一个码包，把 last_tx_ms 刷到最新 */
    kob_client_send_code(&c, v, 1, "E", 1200 + KOB_ID_BEACON_MS - 1);

    int before = g_sent_n;
    kob_client_tick(&c, 1200 + KOB_ID_BEACON_MS);

    /* 恰好一个包：保活 CON 因为 last_tx_ms 刚刷新而不该发，ID 信标必须发。
       信标要是蹭 last_tx_ms，这里一个包也不会有。 */
    ASSERT_EQ_INT(g_sent_n, before + 1);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[before], g_sent_len[before], &p));
    ASSERT_EQ_INT(p.n, 0);
}

TEST(peer_id_triggers_immediate_id_reply) {
    /*
     * 发现延迟不能等于信标周期。对方报身份时立刻回报自己的，一个来回就互相
     * 看见了；干等下一拍的话，一进线路顶栏要愣最多 10 秒才从 0 人变 1 人。
     */
    kob_client_t c; setup(&c);
    bring_online(&c);                       /* 这一步发过 ID，last_id_ms = 1200 */
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_id(pkt, "BG7XXX", 3, "x");
    int before = g_sent_n;
    kob_client_rx(&c, pkt, sizeof pkt, 1200 + KOB_ID_REPLY_MS);
    ASSERT_EQ_INT(g_sent_n, before + 1);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[before], g_sent_len[before], &p));
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_EQ_STR(p.id, "BI8SYN");
}

TEST(id_reply_is_rate_limited) {
    /* 互相应答不能滚成风暴：刚发过 ID 就不再应答。这条是收敛的唯一保证。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_id(pkt, "BG7XXX", 3, "x");
    int before = g_sent_n;
    kob_client_rx(&c, pkt, sizeof pkt, 1200 + KOB_ID_REPLY_MS - 1);
    ASSERT_EQ_INT(g_sent_n, before);
    ASSERT_EQ_STR(g_peer, "BG7XXX");        /* 不应答，但站点照样要记下来 */
}

TEST(code_packet_also_reports_peer_alive) {
    /* 只认 ID 包会把正在连续发报的站点判成失联。码包同样是「还活着」的证据。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 5, v, 3, "A");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_STR(g_peer, "BG7XXX");
    ASSERT_EQ_INT(g_code_n, 3);      /* 报活的同时，码包本身照常送出去 */
}

TEST(duplicate_seq_is_dropped) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 5, v, 3, "A");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_INT(g_code_n, 3);
    g_code_n = -1;
    kob_client_rx(&c, pkt, sizeof pkt, 1600);
    ASSERT_EQ_INT(g_code_n, -1);
}

TEST(id_packet_from_other_reports_peer_not_code) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_id(pkt, "BG7XXX", 1, "MKOB 4.0");
    g_code_n = -1;
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_STR(g_peer, "BG7XXX");
    ASSERT_EQ_INT(g_code_n, -1);
}

TEST(tick_sends_keepalive) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    int before = g_sent_n;
    kob_client_tick(&c, 1200 + KOB_KEEPALIVE_MS + 1);
    ASSERT_TRUE(g_sent_n > before);
}

TEST(tick_does_not_spam_before_keepalive_due) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    int before = g_sent_n;
    kob_client_tick(&c, 1300);
    kob_client_tick(&c, 1400);
    ASSERT_EQ_INT(g_sent_n, before);
}

TEST(silence_drops_back_to_connecting) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
    kob_client_tick(&c, 1200 + KOB_TIMEOUT_MS + 1);
    ASSERT_EQ_INT(c.state, KOB_ST_CONNECTING);
    ASSERT_EQ_INT(g_state, KOB_ST_CONNECTING);
}

TEST(reconnect_after_timeout_registers_again) {
    /* 重连后如果不再发一次 ID 包，就会回到「显示在线但对方收不到」的状态 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    kob_client_tick(&c, 1200 + KOB_TIMEOUT_MS + 1);
    ASSERT_EQ_INT(c.state, KOB_ST_CONNECTING);
    int before = g_sent_n;
    kob_client_rx(&c, ACK, sizeof ACK, 1200 + KOB_TIMEOUT_MS + 100);
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
    ASSERT_TRUE(g_sent_n > before);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[g_sent_n - 1], g_sent_len[g_sent_n - 1], &p));
    ASSERT_EQ_INT(p.n, 0);
    ASSERT_EQ_STR(p.id, "BI8SYN");
}

TEST(stop_sends_dis_and_goes_idle) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    int before = g_sent_n;
    kob_client_stop(&c, 1300);
    ASSERT_EQ_INT(g_sent_n, before + 1);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[before], g_sent_len[before], &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_DIS);
    ASSERT_EQ_INT(c.state, KOB_ST_IDLE);
}

TEST(tick_when_idle_does_nothing) {
    kob_client_t c; setup(&c);
    kob_client_tick(&c, 999999);
    ASSERT_EQ_INT(g_sent_n, 0);
    ASSERT_EQ_INT(c.state, KOB_ST_IDLE);
}

TEST(connecting_retries_con_on_tick) {
    kob_client_t c; setup(&c);
    kob_client_start(&c, 1000);
    int before = g_sent_n;
    kob_client_tick(&c, 1000 + KOB_KEEPALIVE_MS + 1);
    ASSERT_TRUE(g_sent_n > before);
    kob_packet_t p;
    ASSERT_TRUE(kob_pkt_parse(g_sent[g_sent_n - 1], g_sent_len[g_sent_n - 1], &p));
    ASSERT_EQ_INT(p.cmd, KOB_CMD_CON);
}

TEST(millis_wraparound_does_not_hang_keepalive) {
    /* uint32 毫秒在 49.7 天回绕。用裸减法配合有符号比较会在这里误判成
       「超时了几十天」，把在线连接直接掐掉。 */
    kob_client_t c; setup(&c);
    kob_client_start(&c, 0xFFFFF000u);
    kob_client_rx(&c, ACK, sizeof ACK, 0xFFFFF100u);
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
    int before = g_sent_n;
    kob_client_tick(&c, 0x00000100u);   /* 已回绕，实际只过去约 4 秒 */
    ASSERT_EQ_INT(g_sent_n, before);
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
}

TEST(garbage_packet_does_not_change_state) {
    kob_client_t c; setup(&c);
    bring_online(&c);
    uint8_t junk[77];
    memset(junk, 0xAB, sizeof junk);
    kob_client_rx(&c, junk, sizeof junk, 1500);
    ASSERT_EQ_INT(c.state, KOB_ST_ONLINE);
    ASSERT_EQ_INT(g_code_n, -1);
}

TEST(seq_gap_is_reported_as_lost) {
    /* UDP 会丢包，而协议是一个字符一个包——丢了就是丢了一个字。seq 跳变是
       唯一能发现它的线索，界面据此显示 ? 而不是假装什么都没发生。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60, -60, 180 };
    uint8_t pkt[KOB_PKT_LONG];

    kob_pkt_code(pkt, "BG7XXX", 5, v, 3, "A");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_INT(g_lost, 0);

    kob_pkt_code(pkt, "BG7XXX", 8, v, 3, "B");   /* 6、7 丢了 */
    kob_client_rx(&c, pkt, sizeof pkt, 1600);
    ASSERT_EQ_INT(g_lost, 2);
}

TEST(first_packet_from_sender_reports_no_loss) {
    /* 首包没有"上一个 seq"可比，不能把对方的起始 seq 当成丢了一堆包 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 900, v, 1, "E");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    ASSERT_EQ_INT(g_lost, 0);
}

TEST(sender_change_resets_loss_tracking) {
    /* seq 是每个发报方各自计数的。换人时拿旧序号去比会算出天文数字。 */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 500, v, 1, "E");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    kob_pkt_code(pkt, "BD4YYY", 3, v, 1, "T");
    kob_client_rx(&c, pkt, sizeof pkt, 1600);
    ASSERT_EQ_INT(g_lost, 0);
}

TEST(absurd_gap_is_clamped) {
    /* seq 回绕或对方重启时差值可能极大，不能据此塞几万个 ? */
    kob_client_t c; setup(&c);
    bring_online(&c);
    const int32_t v[] = { 60 };
    uint8_t pkt[KOB_PKT_LONG];
    kob_pkt_code(pkt, "BG7XXX", 1, v, 1, "E");
    kob_client_rx(&c, pkt, sizeof pkt, 1500);
    kob_pkt_code(pkt, "BG7XXX", 100000, v, 1, "T");
    kob_client_rx(&c, pkt, sizeof pkt, 1600);
    ASSERT_TRUE(g_lost >= 0 && g_lost <= KOB_LOST_MAX);
}

int main(void) {
    RUN(start_sends_con_and_enters_connecting);
    RUN(ack_triggers_id_registration_then_online);
    RUN(cannot_send_code_before_online);
    RUN(send_code_increments_seq);
    RUN(incoming_code_from_others_is_reported);
    RUN(own_echo_is_ignored);
    RUN(own_id_echo_is_ignored);
    RUN(seq_base_is_seeded_and_above_legacy_range);
    RUN(same_callsign_peer_is_delivered);
    RUN(same_callsign_peer_id_packet_reports_peer);
    RUN(id_beacon_resent_periodically_while_online);
    RUN(id_beacon_not_starved_by_outgoing_code);
    RUN(peer_id_triggers_immediate_id_reply);
    RUN(id_reply_is_rate_limited);
    RUN(code_packet_also_reports_peer_alive);
    RUN(duplicate_seq_is_dropped);
    RUN(id_packet_from_other_reports_peer_not_code);
    RUN(tick_sends_keepalive);
    RUN(tick_does_not_spam_before_keepalive_due);
    RUN(silence_drops_back_to_connecting);
    RUN(reconnect_after_timeout_registers_again);
    RUN(stop_sends_dis_and_goes_idle);
    RUN(tick_when_idle_does_nothing);
    RUN(connecting_retries_con_on_tick);
    RUN(millis_wraparound_does_not_hang_keepalive);
    RUN(garbage_packet_does_not_change_state);
    RUN(seq_gap_is_reported_as_lost);
    RUN(first_packet_from_sender_reports_no_loss);
    RUN(sender_change_resets_loss_tracking);
    RUN(absurd_gap_is_clamped);
    TEST_MAIN_END();
}
