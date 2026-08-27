#include "kob_rxbuf.h"
#include <string.h>

#define BREAK_THRESHOLD_MS 3000   /* 规范：(-n, +2) 且 n > 3000 视为打断 */
#define LATCH_START        1      /* +1 */
#define LATCH_END          2      /* +2 */

/* 见 kob_client.c 里同名函数的说明：uint32 毫秒 49.7 天回绕，
   无符号减法在回绕处依然给出正确差值。 */
static bool reached(uint32_t now, uint32_t deadline)
{
    return (uint32_t)(now - deadline) < 0x80000000u;
}

static void q_push(kob_rxbuf_t *b, int32_t v)
{
    int next = (b->tail + 1) % KOB_RXBUF_CAP;
    if (next == b->head) {
        /* 满了。丢最旧的一个——网络积压时保新弃旧，听感上是「跳过一段」，
           比整体延迟越拖越长要好。 */
        b->head = (b->head + 1) % KOB_RXBUF_CAP;
    }
    b->q[b->tail] = v;
    b->tail = next;
}

static bool q_pop(kob_rxbuf_t *b, int32_t *out)
{
    if (b->head == b->tail) return false;
    *out = b->q[b->head];
    b->head = (b->head + 1) % KOB_RXBUF_CAP;
    return true;
}

static bool q_peek(const kob_rxbuf_t *b, int32_t *out)
{
    if (b->head == b->tail) return false;
    *out = b->q[b->head];
    return true;
}

void kob_rxbuf_init(kob_rxbuf_t *b, uint16_t delay_ms)
{
    memset(b, 0, sizeof *b);
    b->delay_ms = delay_ms;
}

void kob_rxbuf_reset(kob_rxbuf_t *b)
{
    uint16_t d = b->delay_ms;
    memset(b, 0, sizeof *b);
    b->delay_ms = d;
}

bool kob_rxbuf_is_break(const int32_t *code, int32_t n)
{
    if (!code || n != 2) return false;
    return code[1] == LATCH_END && code[0] < 0 && -code[0] > BREAK_THRESHOLD_MS;
}

void kob_rxbuf_push(kob_rxbuf_t *b, const int32_t *code, int32_t n, uint32_t now_ms)
{
    if (!code || n <= 0) return;

    /* 打断绕过缓冲：立刻清空一切并开路。 */
    if (kob_rxbuf_is_break(code, n)) {
        kob_rxbuf_reset(b);
        return;
    }

    for (int32_t i = 0; i < n; i++) q_push(b, code[i]);

    if (!b->have_ready) {
        b->ready_ms = now_ms + b->delay_ms;
        b->have_ready = true;
    }
}

/* 从队列取下一个元素并把它变成「当前状态 + 结束时刻」。
   返回 false 表示队列空了。 */
static bool next_element(kob_rxbuf_t *b, uint32_t from_ms)
{
    int32_t e;
    while (q_pop(b, &e)) {
        if (e == LATCH_START) {
            b->latched = true;
            b->cur_down = true;
            b->playing = false;      /* 锁存期间不看时钟 */
            return true;
        }
        if (e == LATCH_END) {
            b->latched = false;
            b->cur_down = false;
            b->playing = false;
            return true;
        }
        if (e == 0) continue;        /* 填充位，跳过 */

        b->latched = false;
        b->cur_down = e > 0;
        uint32_t dur = (uint32_t)(e > 0 ? e : -e);
        b->elem_end_ms = from_ms + dur;
        b->playing = true;
        return true;
    }
    return false;
}

/*
 * 锁存态下的队列处理。规范原文有两条解锁条件，两条都要认：
 *   "+2: Signals the end of a long mark. Unlatch the circuit."
 *   "closes its local circuit and leaves it closed until it receives a mark
 *    of length greater than 1 ms"
 *
 * 发报方结束长闭合时发的是 (-n, +2)，其中 n 是整段键闭合的时长。那段时间
 * 我们已经在锁存里过完了，所以这个负元素是**追溯性的**，必须丢弃而不是
 * 当成间隔再播一遍——播了就会平白多出 n 毫秒静音。
 *
 * 不处理这条路径的后果是锁存永远解不开：poll 一进来就因为 latched 返回，
 * 队列里的 +2 再也没有机会被消费。
 */
static void drain_latched(kob_rxbuf_t *b, uint32_t now_ms)
{
    int32_t e;
    while (q_peek(b, &e)) {
        if (e == LATCH_END) {
            q_pop(b, &e);
            b->latched = false;
            b->cur_down = false;
            b->playing = false;
            b->ready_ms = now_ms;      /* 后续元素立即接着播，不再等缓冲 */
            b->have_ready = true;
            return;
        }
        if (e == LATCH_START) { q_pop(b, &e); continue; }   /* 重复的 +1，忽略 */
        if (e < 0 || e == 0) { q_pop(b, &e); continue; }    /* 追溯性时长，丢弃 */
        /* 长度 > 1ms 的 mark 同样解锁，且这个 mark 要真的播出来 */
        b->latched = false;
        b->playing = false;
        b->ready_ms = now_ms;
        b->have_ready = true;
        return;
    }
}

void kob_rxbuf_poll(kob_rxbuf_t *b, uint32_t now_ms, kob_rx_out_t *out)
{
    /* 锁存态：不受时钟推进影响，一直保持到收到 +2 / 一个真 mark，或被 reset。 */
    if (b->latched) {
        drain_latched(b, now_ms);
        if (b->latched) {
            out->key_down = b->cur_down;
            out->latched = true;
            return;
        }
    }

    if (!b->have_ready) {
        out->key_down = false;
        out->latched = false;
        return;
    }

    /* 缓冲延迟还没到，先不出声。 */
    if (!b->playing && !reached(now_ms, b->ready_ms)) {
        out->key_down = false;
        out->latched = false;
        return;
    }

    if (!b->playing) {
        /* 第一次开始播放：从 ready_ms 起算，而不是从 now_ms 起算，
           否则 poll 的调用间隔会被累计成额外的时长漂移。 */
        if (!next_element(b, b->ready_ms)) {
            b->have_ready = false;
            out->key_down = false;
            out->latched = false;
            return;
        }
    }

    /* 推进已经走完的元素。用 while 而不是 if：一次 poll 的间隔可能跨过
       多个短元素（60ms 的点在 5ms tick 下不会跨，但网络积压后补播会）。 */
    while (b->playing && reached(now_ms, b->elem_end_ms)) {
        uint32_t prev_end = b->elem_end_ms;
        if (!next_element(b, prev_end)) {
            b->playing = false;
            b->have_ready = false;
            b->cur_down = false;
            break;
        }
        if (b->latched) break;
    }

    out->key_down = (b->playing || b->latched) ? b->cur_down : false;
    out->latched = b->latched;
}
