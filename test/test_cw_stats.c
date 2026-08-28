#include "test_util.h"
#include "cw_stats.h"

TEST(fresh_stats_have_no_history) {
    cw_stats_t s; cw_stats_init(&s);
    uint8_t pct[CW_STATS_HIST], lesson[CW_STATS_HIST];
    ASSERT_EQ_INT(cw_stats_history(&s, pct, lesson, CW_STATS_HIST), 0);
    char ch[8]; uint8_t ep[8];
    ASSERT_EQ_INT(cw_stats_worst(&s, ch, ep, 8), 0);
}

TEST(add_records_history_in_order) {
    cw_stats_t s; cw_stats_init(&s);
    cw_stats_add(&s, 3, 70, "KMRS", "K");
    cw_stats_add(&s, 3, 90, "KMRS", "");
    cw_stats_add(&s, 4, 80, "KMRSU", "U");
    uint8_t pct[CW_STATS_HIST], lesson[CW_STATS_HIST];
    ASSERT_EQ_INT(cw_stats_history(&s, pct, lesson, CW_STATS_HIST), 3);
    /* 按时间正序：最早的在前 */
    ASSERT_EQ_INT(pct[0], 70);
    ASSERT_EQ_INT(pct[1], 90);
    ASSERT_EQ_INT(pct[2], 80);
    ASSERT_EQ_INT(lesson[2], 4);
}

TEST(history_ring_keeps_newest) {
    /* 环形缓冲写满后应该保留最近的 CW_STATS_HIST 条，而不是最早的 */
    cw_stats_t s; cw_stats_init(&s);
    for (int i = 0; i < CW_STATS_HIST + 10; i++) {
        cw_stats_add(&s, 5, (uint8_t)(i % 101), "K", "");
    }
    uint8_t pct[CW_STATS_HIST], lesson[CW_STATS_HIST];
    int n = cw_stats_history(&s, pct, lesson, CW_STATS_HIST);
    ASSERT_EQ_INT(n, CW_STATS_HIST);
    /* 最后一条应该是最后一次写入的值 */
    ASSERT_EQ_INT(pct[n - 1], (CW_STATS_HIST + 9) % 101);
    /* 第一条应该是第 10 次写入的（前 10 条被挤掉了） */
    ASSERT_EQ_INT(pct[0], 10 % 101);
}

TEST(err_pct_counts_seen_and_wrong) {
    cw_stats_t s; cw_stats_init(&s);
    /* K 出现 4 次错 1 次；M 出现 4 次没错过 */
    cw_stats_add(&s, 2, 75, "KMKM", "K");
    cw_stats_add(&s, 2, 100, "KMKM", "");
    ASSERT_EQ_INT(cw_stats_err_pct(&s, 'K'), 25);   /* 1/4 */
    ASSERT_EQ_INT(cw_stats_err_pct(&s, 'M'), 0);
}

TEST(unseen_char_has_zero_pct) {
    cw_stats_t s; cw_stats_init(&s);
    cw_stats_add(&s, 2, 100, "KM", "");
    ASSERT_EQ_INT(cw_stats_err_pct(&s, 'Z'), 0);
}

TEST(worst_is_sorted_by_error_rate) {
    cw_stats_t s; cw_stats_init(&s);
    /* R 错 3/4，K 错 1/4，M 从没错过 */
    cw_stats_add(&s, 5, 50, "KMRKMRKMRKMR", "RRR");
    cw_stats_add(&s, 5, 90, "", "K");
    char ch[8]; uint8_t ep[8];
    int n = cw_stats_worst(&s, ch, ep, 8);
    ASSERT_TRUE(n >= 2);
    ASSERT_EQ_INT(ch[0], 'R');
    ASSERT_TRUE(ep[0] > ep[1]);
}

TEST(worst_ignores_never_seen_and_never_wrong) {
    cw_stats_t s; cw_stats_init(&s);
    cw_stats_add(&s, 2, 100, "KMKM", "");     /* 全对 */
    char ch[8]; uint8_t ep[8];
    /* 一个错都没有时不该列出任何"最差"字母 */
    ASSERT_EQ_INT(cw_stats_worst(&s, ch, ep, 8), 0);
}

TEST(lowercase_and_unknown_chars_are_ignored) {
    /* shown / wrong 里混进小写或非 Koch 字符不能越界写 */
    cw_stats_t s; cw_stats_init(&s);
    cw_stats_add(&s, 2, 50, "k m @ K", "@k");
    ASSERT_EQ_INT(cw_stats_err_pct(&s, 'K'), 0);   /* 只有大写 K 被记为 seen 1 次、未错 */
}

TEST(counters_saturate_instead_of_wrapping) {
    /* uint16 计数器不能回绕成 0，否则错误率会突然跳成怪值 */
    cw_stats_t s; cw_stats_init(&s);
    for (int i = 0; i < 70000; i++) cw_stats_add(&s, 1, 100, "K", "K");
    ASSERT_TRUE(cw_stats_err_pct(&s, 'K') <= 100);
    char ch[4]; uint8_t ep[4];
    ASSERT_TRUE(cw_stats_worst(&s, ch, ep, 4) >= 0);
}

TEST(reset_clears_everything) {
    cw_stats_t s; cw_stats_init(&s);
    cw_stats_add(&s, 3, 70, "KMR", "K");
    cw_stats_init(&s);
    uint8_t pct[CW_STATS_HIST], lesson[CW_STATS_HIST];
    ASSERT_EQ_INT(cw_stats_history(&s, pct, lesson, CW_STATS_HIST), 0);
    ASSERT_EQ_INT(cw_stats_err_pct(&s, 'K'), 0);
}

int main(void) {
    RUN(fresh_stats_have_no_history);
    RUN(add_records_history_in_order);
    RUN(history_ring_keeps_newest);
    RUN(err_pct_counts_seen_and_wrong);
    RUN(unseen_char_has_zero_pct);
    RUN(worst_is_sorted_by_error_rate);
    RUN(worst_ignores_never_seen_and_never_wrong);
    RUN(lowercase_and_unknown_chars_are_ignored);
    RUN(counters_saturate_instead_of_wrapping);
    RUN(reset_clears_everything);
    TEST_MAIN_END();
}
