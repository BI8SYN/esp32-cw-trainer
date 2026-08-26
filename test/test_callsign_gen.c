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
    /* 验证易混表的关键变化通过生成干扰项的行为体现出来。
     * 使用包含特定字符的目标，验证生成的干扰项的字符替换。 */
    char d[3][CALLSIGN_MAX];

    /* 目标包含 9：应该能生成包含 0 的干扰项，不应该生成包含 6 的 */
    rng_seed(&r, 12);
    bool found_9_to_0 = false;
    for (int i = 0; i < 50; i++) {
        callsign_gen_distractors("A9B5CD", &r, d);
        for (int j = 0; j < 3; j++) {
            if (d[j][1] == '0' && d[j][0] == 'A' && d[j][2] == 'B') found_9_to_0 = true;
        }
    }
    ASSERT_TRUE(found_9_to_0);

    /* 目标包含 T：应该能生成包含 M 的干扰项 */
    rng_seed(&r, 13);
    bool found_T_to_M = false;
    for (int i = 0; i < 50; i++) {
        callsign_gen_distractors("T3B5CD", &r, d);
        for (int j = 0; j < 3; j++) {
            if (d[j][0] == 'M' && strcmp(d[j] + 1, "3B5CD") == 0) found_T_to_M = true;
        }
    }
    ASSERT_TRUE(found_T_to_M);

    /* 目标包含 6：应该能生成包含 5 的干扰项 */
    rng_seed(&r, 14);
    bool found_6_to_5 = false;
    for (int i = 0; i < 50; i++) {
        callsign_gen_distractors("A6B5CD", &r, d);
        for (int j = 0; j < 3; j++) {
            if (d[j][1] == '5' && d[j][0] == 'A' && d[j][2] == 'B') found_6_to_5 = true;
        }
    }
    ASSERT_TRUE(found_6_to_5);
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
