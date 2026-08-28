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
/*
 * 别人发来的码包。code/n 是对端的真实手法时序，txt 是可读文本。
 *
 * lost 是本包之前**丢掉的包数**，由 seq 跳变推出。协议是一个字符一个包、
 * UDP 无重传，丢了就是丢了一个字；界面据此显示 ? 而不是假装什么都没发生。
 */
typedef void (*kob_code_fn)(const char *from, const int32_t *code, int32_t n,
                            const char *txt, int32_t lost, void *user);
/*
 * 对端**还在线上**。ID 包和码包都会触发它，不只是 ID 包。
 *
 * 只认 ID 包是不够的：一个正在连续发报的站点不会去发 ID，把它当成失联移出
 * 站点表是错的。反过来，一个安静旁听的站点不发码包，只能靠 ID 信标证明自己
 * 还在。两种包合起来才是完整的「活着」。
 */
typedef void (*kob_peer_fn)(const char *id, void *user);
typedef void (*kob_state_fn)(kob_state_t st, void *user);

typedef struct {
    kob_state_t  state;
    int16_t      wire;
    char         office[KOB_ID_MAX + 1];
    /*
     * 我发出去的 seq 全部落在 [seq_base, tx_seq] 这个闭区间里，只增不减，
     * 断线重连也不重置。这个区间就是「哪些包是我自己发的」的判据，
     * 见 kob_client_rx() 里的自发包过滤。
     *
     * seq_base 取随机值而不是 0：它要能把我和**同呼号的对端**区分开。
     * 两台设备都从 0 开始数的话号段完全重叠，判据就退化回按 id 比较，
     * 也就退化回那个让同呼号双方互相静默的老 bug。实测服务器对 seq 只做
     * 透传、不做任何解释（发 0x0A3B21C7 起的号段，对端原样收到），
     * 所以随便取。
     */
    int32_t      seq_base;
    int32_t      tx_seq;
    int32_t      last_rx_seq;
    bool         have_rx_seq;
    /* seq 是**每个发报方各自计数**的，换人必须重置，否则拿旧序号去比会算出
       天文数字的丢包数。 */
    char         last_rx_from[KOB_ID_MAX + 1];
    uint32_t     last_tx_ms;
    uint32_t     last_id_ms;         /* 上一次发 ID 信标，见 KOB_ID_BEACON_MS */
    uint32_t     last_rx_ms;
    kob_send_fn  send;
    kob_code_fn  on_code;
    kob_peer_fn  on_peer;
    kob_state_fn on_state;
    void        *user;
} kob_client_t;

#define KOB_KEEPALIVE_MS  10000u
/*
 * ID 信标的间隔。ID 包原来只在建立连接时发一次，于是站点表两头都是错的：
 * 晚加入的人永远看不到早到的人（对方的 ID 早发完了），而已经离开的人永远
 * 留在表里（没有任何东西能证明他走了）。周期重发才让「谁在线上」可被观测。
 *
 * 用自己的时间戳而不是蹭 last_tx_ms：正在连续发报的站点 last_tx_ms 一直在
 * 刷新，蹭它的话信标永远发不出去——而那恰恰是最需要让别人知道你在的时候。
 *
 * 10 秒是照着接收侧的过期时间定的（kob_net.c 的 PEER_TTL_MS = 35s，约三个
 * 信标周期）：丢一两个包不会把人误判成离开。保活的 CON 也是 10 秒，两者
 * 同拍发出；服务器侧 TIMEOUT=90s，余量充足。
 */
#define KOB_ID_BEACON_MS  10000u
/*
 * 收到别人的 ID 之后，多久之内不再回报自己的身份。
 *
 * 光有信标，发现延迟就是「等对方下一拍」——平均 5 秒、最坏 10 秒，一进线路
 * 顶栏要愣半天才从 0 人变成 1 人。所以收到别人报身份时立刻回报一次自己的：
 * 新人一上线报出身份，线上所有人当场应答，一个来回（几十毫秒）就互相看见了。
 *
 * 限速是防止互相应答滚成风暴。收敛过程：A 报身份 -> B 应答 -> A 收到 B 的应答，
 * 但 A 刚发过（几十毫秒前），落在窗口内，不再应答，到此为止。窗口取 2 秒，
 * 远大于线路往返（实测 30ms），也远小于信标周期，不会互相干扰。
 */
#define KOB_ID_REPLY_MS   2000u
#define KOB_TIMEOUT_MS    30000u
#define KOB_VERSION_STR   "CWTRN 1.0"
/* 丢包数的上限。seq 回绕或对端重启时差值可能极大，不能据此塞几万个 ?。 */
#define KOB_LOST_MAX      8

/* seed 用来决定 seq 号段的起点，见结构体里 seq_base 的说明。设备上传
   esp_random()，主机测试里传固定值以保证可复现。 */
void kob_client_init(kob_client_t *c, const char *office, int16_t wire,
                     uint32_t seed);
void kob_client_start(kob_client_t *c, uint32_t now_ms);
void kob_client_stop(kob_client_t *c, uint32_t now_ms);
/* 周期调用（建议 100~500ms 一次），驱动保活与超时判定。 */
void kob_client_tick(kob_client_t *c, uint32_t now_ms);
void kob_client_rx(kob_client_t *c, const uint8_t *buf, size_t len, uint32_t now_ms);
/* 仅在 ONLINE 时成功；返回是否真的发出去了。 */
bool kob_client_send_code(kob_client_t *c, const int32_t *code, int32_t n,
                          const char *txt, uint32_t now_ms);

#endif
