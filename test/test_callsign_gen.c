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

TEST(distractors_preserve_shape)
{
    rng_t r;
    char d[3][CALLSIGN_MAX];
    /* 测试多个种子和随机生成的目标，确保形状不变 */
    for (int seed = 1; seed <= 10; seed++) {
        rng_seed(&r, seed);
        char target[CALLSIGN_MAX];
        for (int iter = 0; iter < 20; iter++) {
            ASSERT_TRUE(callsign_gen_random(CALLSIGN_FULL_CHARSET, &r, target));
            callsign_gen_distractors(target, &r, d);
            size_t tlen = strlen(target);
            for (int a = 0; a < 3; a++) {
                /* 干扰项长度应与目标相同 */
                ASSERT_EQ_INT((int)strlen(d[a]), (int)tlen);
                /* 恰好一个数字，不在首位也不在末位 */
                int digits = 0, dpos = -1;
                for (size_t j = 0; j < tlen; j++) if (isdigit((unsigned char)d[a][j])) { digits++; dpos = (int)j; }
                ASSERT_EQ_INT(digits, 1);
                ASSERT_TRUE(dpos >= 1 && dpos < (int)tlen - 1);
                /* 恰好一个字符不同 */
                int diff = 0;
                for (size_t j = 0; j < tlen; j++) if (d[a][j] != target[j]) diff++;
                ASSERT_EQ_INT(diff, 1);
                /* 与目标不同 */
                ASSERT_TRUE(strcmp(d[a], target) != 0);
            }
            /* 三个干扰项互不相同 */
            for (int a = 0; a < 3; a++) {
                for (int b = a + 1; b < 3; b++) {
                    ASSERT_TRUE(strcmp(d[a], d[b]) != 0);
                }
            }
        }
    }
}

TEST(confusable_table_via_distractors)
{
    rng_t r;
    char d[3][CALLSIGN_MAX];

    /* 验证 9↔0（新对）替换成功，且 9 不再与 6 混淆（旧的错误对）。
     * 从含 9 的目标生成多个干扰项，收集所有在 9 位置上的替代字符。
     * 预期：可以看到 0，不应该看到 6（这会在旧表中失败）。 */
    rng_seed(&r, 12);
    bool seen_9_to_0 = false, seen_9_to_6 = false;
    for (int i = 0; i < 100; i++) {
        callsign_gen_distractors("A9B5CD", &r, d);
        for (int j = 0; j < 3; j++) {
            if (d[j][0] == 'A' && d[j][2] == 'B' && d[j][3] == '5' && d[j][4] == 'C' && d[j][5] == 'D') {
                if (d[j][1] == '0') seen_9_to_0 = true;
                if (d[j][1] == '6') seen_9_to_6 = true;
            }
        }
    }
    ASSERT_TRUE(seen_9_to_0);      /* 新表中 9 与 0 相关联 */
    ASSERT_TRUE(!seen_9_to_6);     /* 新表中 9 不与 6 相关联（这在旧表中会失败） */

    /* 验证 6↔5（新对）替换成功。从含 6 的目标生成干扰项，
     * 验证可以看到 5 替代 6（这在旧表中不存在）。 */
    rng_seed(&r, 14);
    bool seen_6_to_5 = false;
    for (int i = 0; i < 100; i++) {
        callsign_gen_distractors("A6B3CD", &r, d);
        for (int j = 0; j < 3; j++) {
            if (d[j][0] == 'A' && d[j][2] == 'B' && d[j][3] == '3' && d[j][4] == 'C' && d[j][5] == 'D') {
                if (d[j][1] == '5') seen_6_to_5 = true;
            }
        }
    }
    ASSERT_TRUE(seen_6_to_5);      /* 新表中 6 与 5 相关联（旧表中不存在此对） */
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
    RUN(distractors_preserve_shape);
    RUN(confusable_table_via_distractors);
    RUN(rst_options_distinct);
    TEST_MAIN_END();
}
