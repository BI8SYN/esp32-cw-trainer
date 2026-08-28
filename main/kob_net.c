#include "kob_net.h"
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "kob_client.h"
#include "kob_rxbuf.h"
#include "net_store.h"
#include "wifi_net.h"
#include "key_input.h"
#include "tone_gen.h"

static const char *TAG = "kob_net";

#define TICK_MS          5      /* 收报回放的精度；5ms 对 60ms 的点足够 */
#define RECV_TIMEOUT_MS  20
#define WIFI_WAIT_MS     30000

/* 发报侧累积缓冲。一个字符最多 51 个元素（协议上限），留满即可。 */
#define TX_ELEM_MAX      KOB_CODE_MAX

#define TXT_RING 8

static kob_net_status_t  s_st;
/* 单生产者（socket 任务）单消费者（UI 轮询）环形队列。head/tail 都是 volatile
   int，且各自只被一侧写，因此不需要锁——见 kob_net.h 里为什么不能用回调。
   满时丢**新**的：丢旧的要动 head，那是消费者的字段，会破坏 SPSC 的前提。 */
static kob_net_line_t    s_ring[TXT_RING];
static volatile int      s_ring_head, s_ring_tail;
static TaskHandle_t      s_task;
static volatile bool     s_stop;
static int               s_sock = -1;
static struct sockaddr_in s_peer_addr;
static kob_client_t      s_client;
static kob_rxbuf_t       s_rxbuf;

/* 发报累积：由 key_input 的边沿回调（core 1，优先级 22）写，socket 任务读。 */
static volatile int32_t  s_tx_elem[TX_ELEM_MAX];
static volatile int      s_tx_n;
static volatile uint32_t s_tx_last_edge_ms;
static volatile bool     s_tx_have_edge;
static volatile bool     s_tx_flush;      /* 字符边界到了，请 socket 任务发出 */
static volatile bool     s_tx_break_req;  /* 请求发一个打断包 */
static char              s_tx_txt[8];

static const char NET_OWNER;   /* 电键归属令牌，见 key_input_stop_if() */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void push_line(const char *from, const char *txt)
{
    int tail = s_ring_tail;
    int next = (tail + 1) % TXT_RING;
    if (next == s_ring_head) return;          /* 满了，丢新的 */
    snprintf(s_ring[tail].from, sizeof s_ring[tail].from, "%s", from ? from : "");
    snprintf(s_ring[tail].txt, sizeof s_ring[tail].txt, "%s", txt ? txt : "");
    s_ring_tail = next;                       /* 内容写完再挪 tail */
}

bool kob_net_pop_line(kob_net_line_t *out)
{
    int head = s_ring_head;
    if (head == s_ring_tail) return false;
    *out = s_ring[head];
    s_ring_head = (head + 1) % TXT_RING;
    return true;
}

static void set_state(kob_net_state_t st)
{
    if (s_st.state == st) return;
    s_st.state = st;
}

/* ------------------------------------------------------------------ 收报 */

static void on_code(const char *from, const int32_t *code, int32_t n,
                    const char *txt, int32_t lost, void *user)
{
    (void)user;

    /* 别人开始发报时，本机若正在发，就是被打断了。 */
    if (s_st.i_am_sending) {
        s_st.broken = true;
    }
    snprintf(s_st.sender, sizeof s_st.sender, "%s", from);

    kob_rxbuf_push(&s_rxbuf, code, n, now_ms());

    /* seq 跳变说明中间丢了包。协议一个字符一个包、UDP 无重传，丢了就是丢了
       一个字——如实显示 ? 而不是把残缺的内容伪装成完整的。 */
    if (lost > 0) {
        char q[KOB_LOST_MAX + 1];
        int k = lost > KOB_LOST_MAX ? KOB_LOST_MAX : (int)lost;
        for (int i = 0; i < k; i++) q[i] = '?';
        q[k] = '\0';
        push_line(from, q);
    }

    if (txt && txt[0]) push_line(from, txt);
}

/*
 * 站点表的过期时间。对端每 10 秒发一次 ID 信标（KOB_ID_BEACON_MS），发报时
 * 码包也算数，所以 35 秒 = 约三个信标周期：丢一两个包不会把人误判成离开，
 * 而真正离线的人半分钟内就会从顶栏消失。
 *
 * 必须有这个：协议里没有「某人退出了」的通知——DIS 是发给服务器的，服务器
 * 不转发。不设过期，站点表就只增不减，顶栏的人数会一直虚高。
 */
#define PEER_TTL_MS 35000u

static uint32_t s_peer_ms[KOB_NET_PEER_MAX];   /* 与 s_st.peers 一一对应 */

/* 对端还在线上。ID 包和码包都会走到这里，见 kob_peer_fn 的说明。 */
static void on_peer(const char *id, void *user)
{
    (void)user;
    uint32_t t = now_ms();
    for (int i = 0; i < s_st.peer_n; i++) {
        if (strcmp(s_st.peers[i], id) == 0) { s_peer_ms[i] = t; return; }
    }
    if (s_st.peer_n < KOB_NET_PEER_MAX) {
        snprintf(s_st.peers[s_st.peer_n], sizeof s_st.peers[0], "%s", id);
        s_peer_ms[s_st.peer_n] = t;
        s_st.peer_n++;
    }
}

/*
 * 把超时未出现的站点摘掉。倒着扫，删除时把末尾那个搬过来即可——顶栏只显示
 * 人数，表内顺序没有意义，不必保序。
 *
 * **自己读钟，绝不能接收主循环那个 t。** on_peer() 里的时间戳也是现读的，
 * 而它发生在 recv() 之后——recv 最多阻塞 20ms，所以 on_peer 打的戳会比循环
 * 顶部的 t 晚。拿 t 去减一个更晚的戳，无符号下溢成 42 亿，远大于 PEER_TTL_MS，
 * 站点刚加进来就被当成超时摘掉。表现是人数在 0 和 1 之间反复闪。
 * 两处读同一个单调时钟、且这里必然读在后面，差值才有意义。
 */
static void peer_sweep(void)
{
    uint32_t now = now_ms();
    for (int i = s_st.peer_n - 1; i >= 0; i--) {
        if ((uint32_t)(now - s_peer_ms[i]) < PEER_TTL_MS) continue;
        int last = s_st.peer_n - 1;
        if (i != last) {
            memcpy(s_st.peers[i], s_st.peers[last], sizeof s_st.peers[0]);
            s_peer_ms[i] = s_peer_ms[last];
        }
        s_st.peers[last][0] = '\0';
        s_st.peer_n--;
    }
}

static void on_client_state(kob_state_t st, void *user)
{
    (void)user;
    if (st == KOB_ST_ONLINE) set_state(KOB_NET_ONLINE);
    else if (st == KOB_ST_CONNECTING) {
        /* 已经在线过一次再回到 CONNECTING，说明是掉线重连，与首次连接
           要分开显示——用户看到的文案不同。 */
        set_state(s_st.state == KOB_NET_ONLINE || s_st.state == KOB_NET_RECONNECTING
                  ? KOB_NET_RECONNECTING : KOB_NET_CONNECTING);
    }
}

/* ------------------------------------------------------------------ 发报 */

/*
 * 键控边沿回调。跑在 key_input 的 5ms 任务上（core 1，优先级 22，全系统第二
 * 高），所以这里只做最少的事：记一个元素，置个标志。真正的打包和 sendto
 * 交给 socket 任务，绝不能在这里做——阻塞这个任务会直接毁掉发报时序。
 */
static void on_key_edge(bool down, uint32_t t_ms, void *user)
{
    (void)user;

    if (!s_tx_have_edge) {
        /* 第一个边沿：没有"上一段"可记，直接开始。 */
        s_tx_have_edge = true;
        s_tx_last_edge_ms = t_ms;
        s_st.i_am_sending = down;
        return;
    }

    int32_t dur = (int32_t)(t_ms - s_tx_last_edge_ms);
    if (dur <= 0) dur = 1;
    s_tx_last_edge_ms = t_ms;

    /* down 变 true 表示刚结束的是一段"断开"，记负数；反之记正数。 */
    int32_t elem = down ? -dur : dur;

    int n = s_tx_n;
    if (n < TX_ELEM_MAX) {
        s_tx_elem[n] = elem;
        s_tx_n = n + 1;
    }
    /* 满了就丢新的：一个字符不该超过 51 个元素，到这一步说明对方在长按或
       手法异常，丢弃比覆盖旧数据（会把已记的时序搅乱）安全。 */

    s_st.i_am_sending = down;
}

/* 字符解码完成 = 一个字符的时序攒齐了，可以发出去。 */
static void on_key_char(char c, void *user)
{
    (void)user;
    if (c == ' ') return;               /* 词间隔不单独发包 */
    s_tx_txt[0] = c;
    s_tx_txt[1] = '\0';
    s_tx_flush = true;
}

/* ------------------------------------------------------------------ socket */

static bool sock_open(void)
{
    const cw_net_cfg_t *cfg = net_store_get();

    memset(&s_peer_addr, 0, sizeof s_peer_addr);
    s_peer_addr.sin_family = AF_INET;
    s_peer_addr.sin_port = htons(cfg->port);

    /* 地址可能是域名也可能是点分十进制，统一走 getaddrinfo。 */
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(cfg->host, NULL, &hints, &res) != 0 || !res) {
        ESP_LOGE(TAG, "cannot resolve %s", cfg->host);
        return false;
    }
    s_peer_addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return false;
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = RECV_TIMEOUT_MS * 1000 };
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    ESP_LOGI(TAG, "socket -> %s:%u wire=%d(%s) office=%s",
             cfg->host, cfg->port, cfg->wire,
             cw_net_wire_label(cfg->wire) ? cw_net_wire_label(cfg->wire) : "自定义",
             cw_net_office());
    return true;
}

static void sock_send(const uint8_t *buf, size_t len, void *user)
{
    (void)user;
    if (s_sock < 0) return;
    int n = sendto(s_sock, buf, len, 0, (struct sockaddr *)&s_peer_addr, sizeof s_peer_addr);
    if (n < 0) ESP_LOGW(TAG, "sendto failed: errno %d", errno);
}

/* ------------------------------------------------------------------ 主任务 */

static void flush_tx(void)
{
    int n = s_tx_n;
    if (n <= 0) { s_tx_flush = false; return; }

    int32_t elems[TX_ELEM_MAX];
    for (int i = 0; i < n; i++) elems[i] = s_tx_elem[i];
    s_tx_n = 0;
    s_tx_flush = false;

    char txt[8];
    snprintf(txt, sizeof txt, "%s", s_tx_txt);

    if (kob_client_send_code(&s_client, elems, n, txt, now_ms())) {
        push_line("", txt);
    }
}

static void send_break(void)
{
    s_tx_break_req = false;
    /* 规范：(-n, +2) 且 n > 3000 表示打断。 */
    const int32_t brk[2] = { -3500, 2 };
    kob_client_send_code(&s_client, brk, 2, "", now_ms());
}

static void task(void *arg)
{
    (void)arg;

    /* 1. WiFi */
    set_state(KOB_NET_WIFI);
    for (int waited = 0; waited < WIFI_WAIT_MS && !s_stop; waited += 100) {
        if (wifi_net_is_connected()) break;
        wifi_net_state_t ws = wifi_net_state();
        if (ws == WIFI_NET_FAIL_NO_AP || ws == WIFI_NET_FAIL_AUTH) {
            snprintf(s_st.wifi_ssid, sizeof s_st.wifi_ssid, "%s", wifi_net_ssid());
            set_state(KOB_NET_ERR_WIFI);
            s_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (s_stop || !wifi_net_is_connected()) {
        set_state(KOB_NET_ERR_WIFI);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    snprintf(s_st.wifi_ssid, sizeof s_st.wifi_ssid, "%s", wifi_net_ssid());

    /* 2. socket + 协议 */
    if (!sock_open()) {
        set_state(KOB_NET_ERR_SERVER);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    const cw_net_cfg_t *cfg = net_store_get();
    /* wire 是唯一的控制项——不同的 wire 上跑着不同的陪练。 */
    /* seq 号段起点要随机——它是「这个包是不是我自己发的」的判据，和同呼号
       的对端区分开全靠它。见 kob_client.h 里 seq_base 的说明。 */
    kob_client_init(&s_client, cw_net_office(), cfg->wire, esp_random());
    s_client.send = sock_send;
    s_client.on_code = on_code;
    s_client.on_peer = on_peer;
    s_client.on_state = on_client_state;
    kob_rxbuf_init(&s_rxbuf, cfg->rx_delay_ms);

    set_state(KOB_NET_CONNECTING);
    kob_client_start(&s_client, now_ms());

    /* 3. 接管电键 */
    s_tx_n = 0;
    s_tx_have_edge = false;
    s_tx_flush = false;
    /* 先 start 再挂边沿：start 会清掉上一个页面留下的边沿回调。 */
    key_input_start(on_key_char, (void *)&NET_OWNER);
    key_input_set_edge_cb(on_key_edge, NULL);

    /* 4. 主循环 */
    uint32_t last_online_check = now_ms();
    while (!s_stop) {
        uint32_t t = now_ms();

        uint8_t rx[KOB_PKT_LONG];
        int n = recv(s_sock, rx, sizeof rx, 0);
        if (n > 0) kob_client_rx(&s_client, rx, (size_t)n, t);

        kob_client_tick(&s_client, t);

        /* 收报回放 -> 侧音。注意：这里和 key_input 的 5ms 任务都会调
           tone_gen_set()。本机发报时不要让收报把侧音抢走。 */
        kob_rx_out_t out;
        kob_rxbuf_poll(&s_rxbuf, t, &out);
        if (!s_st.i_am_sending) tone_gen_set(out.key_down);

        if (s_tx_break_req) send_break();
        if (s_tx_flush) flush_tx();

        peer_sweep();

        /* 线路空闲判定：一段时间没有新的收报元素，就认为对方发完了。 */
        if (!out.key_down && !out.latched && s_st.sender[0] &&
            (uint32_t)(t - last_online_check) > 1500) {
            s_st.sender[0] = '\0';
            s_st.broken = false;
        }
        if (out.key_down) last_online_check = t;

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }

    /* 5. 收尾 */
    key_input_stop_if((void *)&NET_OWNER);
    tone_gen_set(false);
    kob_client_stop(&s_client, now_ms());
    if (s_sock >= 0) { close(s_sock); s_sock = -1; }
    set_state(KOB_NET_OFF);
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ 公开 */

/* 停 socket 任务并等它自己收尾（关 socket、摘电键回调）。**不碰 WiFi**。 */
static void stop_task(void)
{
    if (!s_task) return;
    s_stop = true;
    /* 这个等待是安全的：kob_net 不再回调进 UI，socket 任务不会去取 ui_lock()，
       因此不会和持有该锁的 LVGL 线程互等。任务循环 5ms 一拍、recv 超时 20ms，
       正常几十毫秒内就退出。 */
    for (int i = 0; i < 100 && s_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t spawn_task(void)
{
    memset(&s_st, 0, sizeof s_st);
    memset(s_peer_ms, 0, sizeof s_peer_ms);
    s_ring_head = s_ring_tail = 0;
    s_stop = false;

    /* 钉在 core 0：和 WiFi/lwIP 同核。优先级 5，远低于 core 1 上的 tone(23)
       与 key_input(22)——发报时序的优先级高于一切网络处理。 */
    if (xTaskCreatePinnedToCore(task, "kob_net", 4096, NULL, 5, &s_task, 0) != pdPASS) {
        s_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t kob_net_start(void)
{
    if (s_task) return ESP_ERR_INVALID_STATE;

    esp_err_t err = wifi_net_init();
    if (err != ESP_OK) return err;
    err = wifi_net_start(NULL, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    return spawn_task();
}

/*
 * 换 wire。只重开 socket 和协议握手，**射频一根手指都不碰**。
 *
 * 换 wire 曾经是走「整页重建」的（apply_wire() -> ui_nav_show(ui_net_create)），
 * 而重建会连带 kob_net_stop() -> wifi_net_stop() -> esp_wifi_deinit()，把几十 KB
 * 的 WiFi 驱动整个卸掉再装回来，全程堵在 LVGL 事件回调里。更糟的是
 * wifi_net_stop() 并不等 wifi_net 那个连接 worker 退出，deinit 会从它脚下把
 * 驱动抽走。换一条线路而已，不该动到射频。
 */
esp_err_t kob_net_rewire(void)
{
    stop_task();
    return spawn_task();
}

void kob_net_stop(void)
{
    stop_task();
    /* 离开对练功能才关射频——那几十 KB 的驱动要还回堆里。换 wire 走
       kob_net_rewire()，不到这里。 */
    wifi_net_stop();
}

const kob_net_status_t *kob_net_status(void) { return &s_st; }
