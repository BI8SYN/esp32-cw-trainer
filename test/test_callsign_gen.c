#include "test_util.h"
#include "callsign_gen.h"
#include <string.h>
#include <ctype.h>

TEST(random_callsign_has_valid_shape)
{
    rng_t r; rng_seed(&r, 7);
    char cs[CALLSIGN_MAX];
    for (int i = 0; i < 300; i++) {
        ASSERT_TRUE(callsign_gen_random(CALLSIGN_FULL_CHARSET, &r, cs));
        size_t len = strlen(cs);
        ASSERT_TRUE(len >= 3 && len <= 6);
        /* 恰好一个数字，且不在首位也不在末位 */
        int digits = 0, dpos = -1;
        for (size_t j = 0; j < len; j++) if (isdigit((unsigned char)cs[j])) { digits++; dpos = (int)j; }
        ASSERT_EQ_INT(digits, 1);
        ASSERT_TRUE(dpos >= 1 && dpos <= 2 && dpos < (int)len - 1);
    }
}

TEST(random_callsign_respects_charset)
{
    rng_t r; rng_seed(&r, 9);
    char cs[CALLSIGN_MAX];
    const char *charset = "KMRSU5";
    for (int i = 0; i < 100; i++) {
        ASSERT_TRUE(callsign_gen_random(charset, &r, cs));
        for (const char *p = cs; *p; p++) ASSERT_TRUE(strchr(charset, *p) != NULL);
    }
}

TEST(random_callsign_rejects_small_charset)
{
    rng_t r; rng_seed(&r, 1);
    char cs[CALLSIGN_MAX] = "XX";
    ASSERT_TRUE(!callsign_gen_random("KMR5", &r, cs));   /* 3 个字母不够 */
    ASSERT_TRUE(!callsign_gen_random("KMRSU", &r, cs));  /* 没数字 */
    ASSERT_EQ_STR(cs, "XX");
}

TEST(distractors_distinct_and_one_char_apart)
{
    rng_t r; rng_seed(&r, 3);
    char d[3][CALLSIGN_MAX];
    for (int i = 0; i < 100; i++) {
        callsign_gen_distractors("BG5ESN", &r, d);
        for (int a = 0; a < 3; a++) {
            ASSERT_TRUE(strcmp(d[a], "BG5ESN") != 0);
            ASSERT_EQ_INT((int)strlen(d[a]), 6);
            int diff = 0;
            for (int j = 0; j < 6; j++) if (d[a][j] != "BG5ESN"[j]) diff++;
            ASSERT_EQ_INT(diff, 1);
            for (int b = a + 1; b < 3; b++) ASSERT_TRUE(strcmp(d[a], d[b]) != 0);
        }
    }
}

TEST(rst_options_distinct)
{
    rng_t r; rng_seed(&r, 5);
    char o[4][4];
    for (int i = 0; i < 50; i++) {
        callsign_gen_rst(&r, o);
        for (int a = 0; a < 4; a++) {
            ASSERT_EQ_INT((int)strlen(o[a]), 3);
            for (int b = a + 1; b < 4; b++) ASSERT_TRUE(strcmp(o[a], o[b]) != 0);
        }
    }
}

int main(void)
{
    RUN(random_callsign_has_valid_shape);
    RUN(random_callsign_respects_charset);
    RUN(random_callsign_rejects_small_charset);
    RUN(distractors_distinct_and_one_char_apart);
    RUN(rst_options_distinct);
    TEST_MAIN_END();
}
