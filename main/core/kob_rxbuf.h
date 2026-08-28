#ifndef KOB_RXBUF_H
#define KOB_RXBUF_H
#include <stdbool.h>
#include <stdint.h>

/*
 * 收报侧的抖动缓冲，外加 MorseKOB 在 CWCom 上的闭合电路扩展语义。
 * 纯 C，不依赖 ESP-IDF。
 *
 * 时序元素的约定（来自 CWCom 协议规范文档，不是代码，因此不涉 GPL）：
 *   正数 = 键闭合（发声）的毫秒数
 *   负数 = 键断开（静音）的毫秒数
 *   +1   = 长闭合开始，锁存电路闭合
 *   +2   = 长闭合结束，解锁
 *
 * 为什么要缓冲：MorseKOB 的做法是收到的元素先缓冲约 0.5 秒再送去发声，
 * 用来吸收字符长度与网络传输的抖动。自建服务器实测抖动只有 4ms，0.5s
 * 绰绰有余，但缓冲仍然必要——它同时吸收的是发报方手法本身的不匀。
 *
 * 唯一的例外是「打断」：收到 (-n, +2) 且 n > 3000 时必须绕过缓冲立即
 * 开路。否则被打断的一方要迟钝半秒才停，听感上就是「叫不停他」。
 */

/* 待播元素的环形队列容量。256 个 int32 = 1KB，对本项目的堆压力可忽略。
   一个字符最多 51 个元素，所以这个深度能装下约 5 个字符的积压。 */
#define KOB_RXBUF_CAP 256

typedef struct {
    bool key_down;   /* 当前是否应该发声 */
    bool latched;    /* 收到过 +1，电路锁存闭合 */
} kob_rx_out_t;

typedef struct {
    uint16_t delay_ms;
    /*
     * 已经**播完**的字符数，只增不减（reset 时归零）。
     *
     * 文字必须跟着声音走：收到包就把文字推给界面的话，文字会恒定领先声音
     * delay_ms 加上这个字符本身的时长，也就是 0.5~1 秒。缓冲在每个字符的
     * 末尾埋一个边界标记，播到它才 +1，界面据此决定什么时候吐这个字。
     */
    uint32_t chars_done;
    /*
     * 冻结：本机发报期间把时间基准停住。侧音只有一路，对方的音频不能同时
     * 响，但也不能丢——原来是照常推进时钟只不出声，松手时那几个字已经流
     * 过去了。held_at_ms 记下冻结的时刻，resume 时把所有未来时刻整体后移。
     */
    bool     held;
    uint32_t held_at_ms;
    int32_t  q[KOB_RXBUF_CAP];
    int      head, tail;             /* head == tail 表示空 */
    bool     playing;                /* 是否已经开始消费队列 */
    uint32_t elem_end_ms;            /* 当前元素结束的时刻 */
    bool     cur_down;               /* 当前元素是闭合还是断开 */
    bool     latched;
    uint32_t ready_ms;               /* 最早可以开始播放的时刻（push 时刻 + delay） */
    bool     have_ready;
} kob_rxbuf_t;

void kob_rxbuf_init(kob_rxbuf_t *b, uint16_t delay_ms);
/* 投入一个收到的码包。now_ms 是收到的时刻，缓冲延迟从这里起算。 */
void kob_rxbuf_push(kob_rxbuf_t *b, const int32_t *code, int32_t n, uint32_t now_ms);
/* 周期调用（建议 5~10ms），返回此刻应有的发声状态。 */
void kob_rxbuf_poll(kob_rxbuf_t *b, uint32_t now_ms, kob_rx_out_t *out);
void kob_rxbuf_reset(kob_rxbuf_t *b);

/* 已播完的字符数。调用方用它的增量决定吐几个字，见 chars_done 的说明。 */
uint32_t kob_rxbuf_chars_done(const kob_rxbuf_t *b);

/*
 * 冻结 / 解冻播放。本机发报期间冻住，松手解冻，中间这段时间不推进。
 * 两者都是幂等的：重复 hold 不会把冻结起点往后挪，未冻结时 resume 无效。
 */
void kob_rxbuf_hold(kob_rxbuf_t *b, uint32_t now_ms);
void kob_rxbuf_resume(kob_rxbuf_t *b, uint32_t now_ms);
/* 判断一个码包是否是「打断」：恰好两个元素，(-n, +2) 且 n > 3000。 */
bool kob_rxbuf_is_break(const int32_t *code, int32_t n);

#endif
