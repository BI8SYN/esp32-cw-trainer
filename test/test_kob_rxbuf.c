#include "test_util.h"
#include "kob_rxbuf.h"

#define DELAY 500

TEST(elements_play_after_delay_not_before) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60, -60, 180 };
    kob_rxbuf_push(&b, v, 3, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1100, &o);              /* 还没到延迟 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500, &o);              /* 到点，第一个 mark 60ms */
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 1500 + 61, &o);         /* mark 结束，进入 60ms 间隔 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500 + 130, &o);        /* 间隔结束，进入 180ms 的 dah */
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 1500 + 320, &o);        /* 全部放完 */
    ASSERT_TRUE(!o.key_down);
}

TEST(negative_leading_gap_is_honored) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { -300, 60 };
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);              /* 延迟到了，但还在 300ms 前导间隔里 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500 + 310, &o);
    ASSERT_TRUE(o.key_down);
}

TEST(plus_one_latches_circuit_closed) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60, 1 };             /* mark 之后长闭合 */
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1500 + 100, &o);
    ASSERT_TRUE(o.latched);
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 1500 + 100000, &o);     /* 很久以后仍然闭合 */
    ASSERT_TRUE(o.key_down);
    ASSERT_TRUE(o.latched);
}

TEST(plus_two_unlatches) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v1[] = { 60, 1 };
    kob_rxbuf_push(&b, v1, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);
    ASSERT_TRUE(o.latched);
    const int32_t v2[] = { -500, 2 };          /* 500ms 不够 3000，走正常缓冲 */
    kob_rxbuf_push(&b, v2, 2, 2000);
    kob_rxbuf_poll(&b, 2500 + 600, &o);
    ASSERT_TRUE(!o.latched);
    ASSERT_TRUE(!o.key_down);
}

TEST(break_is_detected) {
    const int32_t brk[]    = { -3500, 2 };
    const int32_t nobrk[]  = { -1200, 2 };
    const int32_t plain[]  = { 60, -60, 180 };
    const int32_t notwo[]  = { -3500, 60 };
    ASSERT_TRUE(kob_rxbuf_is_break(brk, 2));
    ASSERT_TRUE(!kob_rxbuf_is_break(nobrk, 2));
    ASSERT_TRUE(!kob_rxbuf_is_break(plain, 3));
    ASSERT_TRUE(!kob_rxbuf_is_break(notwo, 2));
    ASSERT_TRUE(!kob_rxbuf_is_break(brk, 1));
    ASSERT_TRUE(!kob_rxbuf_is_break(NULL, 2));
}

TEST(break_bypasses_buffer_immediately) {
    /* 规范：收到 (-n, +2) 且 n > 3000 视为打断，必须绕过缓冲立即开路，
       否则被打断的一方反应迟钝 500ms，听感上就是「叫不停他」。 */
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60, 1 };
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);
    ASSERT_TRUE(o.key_down);
    const int32_t brk[] = { -3500, 2 };
    kob_rxbuf_push(&b, brk, 2, 2000);
    kob_rxbuf_poll(&b, 2001, &o);              /* 不等 500ms */
    ASSERT_TRUE(!o.key_down);
    ASSERT_TRUE(!o.latched);
}

TEST(reset_clears_everything) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60, 1 };
    kob_rxbuf_push(&b, v, 2, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1600, &o);
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_reset(&b);
    kob_rxbuf_poll(&b, 1700, &o);
    ASSERT_TRUE(!o.key_down);
    ASSERT_TRUE(!o.latched);
}

TEST(two_packets_queue_in_order) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t a[] = { 60 };
    const int32_t c[] = { -60, 180 };
    kob_rxbuf_push(&b, a, 1, 1000);
    kob_rxbuf_push(&b, c, 2, 1010);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 1500, &o);              /* a 的 60ms mark */
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 1500 + 70, &o);         /* c 的 60ms 间隔 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 1500 + 130, &o);        /* c 的 180ms mark */
    ASSERT_TRUE(o.key_down);
}

TEST(zero_elements_push_is_noop) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60 };
    kob_rxbuf_push(&b, v, 0, 1000);
    kob_rxbuf_push(&b, NULL, 3, 1000);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 2000, &o);
    ASSERT_TRUE(!o.key_down);
}

TEST(overflow_drops_oldest_not_crash) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60, -60, 180, -60, 60 };
    for (int i = 0; i < 200; i++) kob_rxbuf_push(&b, v, 5, 1000 + (uint32_t)i);
    kob_rx_out_t o;
    for (uint32_t t = 1000; t < 8000; t += 37) kob_rxbuf_poll(&b, t, &o);
    ASSERT_TRUE(true);   /* 不崩、不越界即可（-fsanitize 下跑更有意义） */
}

TEST(millis_wraparound_still_plays) {
    kob_rxbuf_t b; kob_rxbuf_init(&b, DELAY);
    const int32_t v[] = { 60, -60, 180 };
    kob_rxbuf_push(&b, v, 3, 0xFFFFFE00u);
    kob_rx_out_t o;
    kob_rxbuf_poll(&b, 0xFFFFFE00u + 100, &o);       /* 未到延迟 */
    ASSERT_TRUE(!o.key_down);
    kob_rxbuf_poll(&b, 0xFFFFFE00u + DELAY + 10, &o); /* 已回绕，应开始放 */
    ASSERT_TRUE(o.key_down);
}

TEST(char_boundary_reported_when_its_audio_finishes) {
    /*
     * 文字必须跟着声音走。原来 on_code() 收到包就把文字推给界面，而音频要等
     * 抖动缓冲的 500ms 再播、还要花掉字符本身的时长——文字恒定领先声音
     * 0.5~1 秒。缓冲在每个字符尾部埋一个边界，播完最后一个码元才计数，
     * 界面据此吐字。
     */
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    kob_rx_out_t o;
    const int32_t a[] = { 60, -60, 180 };      /* 共 300ms */
    kob_rxbuf_push(&b, a, 3, 1000);

    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 0);
    kob_rxbuf_poll(&b, 1500, &o);              /* 刚开播 */
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 0);
    kob_rxbuf_poll(&b, 1799, &o);              /* 还差 1ms */
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 0);
    kob_rxbuf_poll(&b, 1800, &o);              /* 300ms 播完 */
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 1);
}

TEST(char_boundaries_counted_per_packet) {
    /* 一个包一个字符，两个包必须分别计数，不能合成一次。 */
    kob_rxbuf_t b; kob_rxbuf_init(&b, 0);
    kob_rx_out_t o;
    const int32_t a[] = { 100 };
    kob_rxbuf_push(&b, a, 1, 0);
    kob_rxbuf_push(&b, a, 1, 0);

    kob_rxbuf_poll(&b, 0, &o);
    kob_rxbuf_poll(&b, 100, &o);
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 1);
    kob_rxbuf_poll(&b, 200, &o);
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 2);
}

TEST(hold_freezes_playback_instead_of_dropping_it) {
    /*
     * 本机发报时侧音只有一路，对方的音频不能同时响——但也**不能丢**。
     * 原来的做法是照常推进时钟、只是不出声，等松手时那几个字已经流过去，
     * 永远听不到。改成冻住时间基准，松手后整体后移接着播。
     */
    kob_rxbuf_t b; kob_rxbuf_init(&b, 0);
    kob_rx_out_t o;
    const int32_t a[] = { 100, -100, 100 };
    kob_rxbuf_push(&b, a, 3, 0);

    kob_rxbuf_poll(&b, 0, &o);
    ASSERT_TRUE(o.key_down);                   /* 第一个 mark 正在响 */

    kob_rxbuf_hold(&b, 50);                    /* 我开始发报 */
    kob_rxbuf_resume(&b, 5050);                /* 按了 5 秒 */

    /* 冻结期间不该有任何推进：mark 还剩 50ms */
    kob_rxbuf_poll(&b, 5099, &o);
    ASSERT_TRUE(o.key_down);
    kob_rxbuf_poll(&b, 5101, &o);
    ASSERT_TRUE(!o.key_down);                  /* 进入 -100 的静音 */
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 0);
    kob_rxbuf_poll(&b, 5301, &o);              /* 三个元素共 300ms 播完 */
    ASSERT_EQ_INT((int)kob_rxbuf_chars_done(&b), 1);
}

TEST(hold_before_playback_defers_the_start) {
    /* 还没开播就被冻住：起播时刻也要跟着后移，否则一松手就「跳过一段」。 */
    kob_rxbuf_t b; kob_rxbuf_init(&b, 500);
    kob_rx_out_t o;
    const int32_t a[] = { 100 };
    kob_rxbuf_push(&b, a, 1, 0);               /* 本该 500ms 起播 */

    kob_rxbuf_hold(&b, 100);
    kob_rxbuf_resume(&b, 2100);                /* 冻了 2 秒 */

    kob_rxbuf_poll(&b, 2400, &o);
    ASSERT_TRUE(!o.key_down);                  /* 起播推到了 2500 */
    kob_rxbuf_poll(&b, 2500, &o);
    ASSERT_TRUE(o.key_down);
}

int main(void) {
    RUN(elements_play_after_delay_not_before);
    RUN(negative_leading_gap_is_honored);
    RUN(plus_one_latches_circuit_closed);
    RUN(plus_two_unlatches);
    RUN(break_is_detected);
    RUN(break_bypasses_buffer_immediately);
    RUN(reset_clears_everything);
    RUN(two_packets_queue_in_order);
    RUN(zero_elements_push_is_noop);
    RUN(overflow_drops_oldest_not_crash);
    RUN(char_boundary_reported_when_its_audio_finishes);
    RUN(char_boundaries_counted_per_packet);
    RUN(hold_freezes_playback_instead_of_dropping_it);
    RUN(hold_before_playback_defers_the_start);
    RUN(millis_wraparound_still_plays);
    TEST_MAIN_END();
}
