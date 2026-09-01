#include <stdbool.h>
#include "test_util.h"
#include "tone_seq.h"

TEST(tap_is_one_short_blip)
{
    tone_step_t s[TONE_SEQ_MAX];
    int n = tone_seq_build(TONE_SEQ_TAP, 600, s, TONE_SEQ_MAX);
    ASSERT_EQ_INT(n, 1);
    ASSERT_EQ_INT(s[0].hz, 1200);
    /* 20ms 必须短于 40 WPM 的点（30ms）——这是「不像莫尔斯」的第一条依据 */
    ASSERT_EQ_INT(s[0].ms, 20);
    ASSERT_TRUE(s[0].ms < 30);
}

TEST(ok_rises_miss_falls)
{
    tone_step_t ok[TONE_SEQ_MAX], miss[TONE_SEQ_MAX];
    int a = tone_seq_build(TONE_SEQ_OK, 600, ok, TONE_SEQ_MAX);
    int b = tone_seq_build(TONE_SEQ_MISS, 600, miss, TONE_SEQ_MAX);
    ASSERT_EQ_INT(a, 3);
    ASSERT_EQ_INT(b, 3);
    ASSERT_TRUE(ok[0].hz < ok[2].hz);      /* 升 */
    ASSERT_TRUE(miss[0].hz > miss[2].hz);  /* 降 */
    /* 互为倒序：不用另记一套，听感上天然成对 */
    ASSERT_EQ_INT(ok[0].hz, miss[2].hz);
    ASSERT_EQ_INT(ok[2].hz, miss[0].hz);
}

/*
 * 这条测试逼出过一个真实的设计错误：异常音最初是单频 120ms，而 30 WPM 的划
 * 正好是 120ms——它会被听成字母 T。规则写在 spec 里，但只有把它变成断言，
 * 才发现表里有一行不守规则。
 */
TEST(every_seq_changes_frequency_or_is_out_of_morse_range)
{
    /* 「不像莫尔斯」的两条依据，逐个事件核一遍：
       要么序列内频率变过（莫尔斯从不变频），要么时长落在点划量级之外。 */
    const tone_seq_id_t ids[] = { TONE_SEQ_TAP, TONE_SEQ_OK, TONE_SEQ_MISS,
                                  TONE_SEQ_UNLOCK, TONE_SEQ_FAULT };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        tone_step_t s[TONE_SEQ_MAX];
        int n = tone_seq_build(ids[i], 600, s, TONE_SEQ_MAX);
        ASSERT_TRUE(n > 0);
        int freqs = 0;
        uint16_t first = 0;
        bool varies = false, out_of_range = true;
        for (int k = 0; k < n; k++) {
            if (!s[k].hz) continue;
            if (!freqs++) first = s[k].hz;
            else if (s[k].hz != first) varies = true;
            /* 点划量级：30..250ms。落在这个区间内的单频音会被听成电码 */
            if (s[k].ms >= 30 && s[k].ms <= 250) out_of_range = false;
        }
        ASSERT_TRUE(varies || out_of_range);
    }
}

TEST(frequency_follows_sidetone_setting)
{
    /* 用户把音调从 400 改到 1000，提示音跟着走，相对关系不变 */
    tone_step_t a[TONE_SEQ_MAX], b[TONE_SEQ_MAX];
    tone_seq_build(TONE_SEQ_TAP, 400, a, TONE_SEQ_MAX);
    tone_seq_build(TONE_SEQ_TAP, 1000, b, TONE_SEQ_MAX);
    ASSERT_EQ_INT(a[0].hz, 800);
    ASSERT_EQ_INT(b[0].hz, 2000);
}

TEST(clamps_extreme_settings)
{
    tone_step_t s[TONE_SEQ_MAX];
    /* 音调设得很高时 ×4 不能窜到刺耳的地方 */
    tone_seq_build(TONE_SEQ_UNLOCK, 1200, s, TONE_SEQ_MAX);
    ASSERT_EQ_INT(s[4].hz, 4000);
    /* 音调很低时 ×0.5 不能掉到听不见 */
    /* 侧音设到最低 300 时，异常音的降调也不能塌成单频——那会变回一个
       能被听成「划」的单频音，正是这套设计要避免的 */
    tone_seq_build(TONE_SEQ_FAULT, 300, s, TONE_SEQ_MAX);
    ASSERT_EQ_INT(s[0].hz, 450);
    ASSERT_EQ_INT(s[2].hz, 300);
    ASSERT_TRUE(s[0].hz > s[2].hz);
}

TEST(respects_output_capacity)
{
    tone_step_t s[2];
    int n = tone_seq_build(TONE_SEQ_UNLOCK, 600, s, 2);
    ASSERT_EQ_INT(n, 2);      /* 装不下就截断，不越界 */
}

int main(void)
{
    RUN(tap_is_one_short_blip);
    RUN(ok_rises_miss_falls);
    RUN(every_seq_changes_frequency_or_is_out_of_morse_range);
    RUN(frequency_follows_sidetone_setting);
    RUN(clamps_extreme_settings);
    RUN(respects_output_capacity);
    TEST_MAIN_END();
}
