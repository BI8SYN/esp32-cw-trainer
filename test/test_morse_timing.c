#include "test_util.h"
#include "morse_timing.h"

TEST(standard_20wpm)
{
    morse_timing_t t;
    morse_timing_compute(20, 20, &t);
    ASSERT_EQ_INT(t.dit_ms, 60);
    ASSERT_EQ_INT(t.dah_ms, 180);
    ASSERT_EQ_INT(t.intra_gap_ms, 60);
    ASSERT_EQ_INT(t.char_gap_ms, 180);
    ASSERT_EQ_INT(t.word_gap_ms, 420);
}

TEST(farnsworth_20_8)
{
    morse_timing_t t;
    morse_timing_compute(20, 8, &t);
    ASSERT_EQ_INT(t.dit_ms, 60);
    /* ta = (60000*20 - 37200*8)/(8*20) = (1200000-297600)/160 = 5640 ms */
    ASSERT_EQ_INT(t.char_gap_ms, 5640 * 3 / 19);   /* 890 */
    ASSERT_EQ_INT(t.word_gap_ms, 5640 * 7 / 19);   /* 2077 */
}

TEST(eff_greater_than_char_uses_standard)
{
    morse_timing_t t;
    morse_timing_compute(15, 30, &t);
    ASSERT_EQ_INT(t.dit_ms, 80);
    ASSERT_EQ_INT(t.char_gap_ms, 240);
}

TEST(clamps)
{
    morse_timing_t t;
    morse_timing_compute(0, 0, &t);
    ASSERT_EQ_INT(t.dit_ms, 240);   /* 5 wpm */
    morse_timing_compute(200, 200, &t);
    ASSERT_EQ_INT(t.dit_ms, 20);    /* 60 wpm */
}

int main(void)
{
    RUN(standard_20wpm);
    RUN(farnsworth_20_8);
    RUN(eff_greater_than_char_uses_standard);
    RUN(clamps);
    TEST_MAIN_END();
}
