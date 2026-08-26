#include "test_util.h"
#include "trainer.h"
#include <string.h>

TEST(koch_ten_rounds_and_percent)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, "BI8SYN", 42);
    ASSERT_EQ_INT((int)strlen(t.target), 5);
    ASSERT_EQ_STR(t.play_text, t.target);
    int rounds = 0;
    do {
        rounds++;
        bool ok = trainer_submit(&t, t.target);   /* 全部答对 */
        ASSERT_TRUE(ok);
    } while (trainer_next(&t));
    ASSERT_EQ_INT(rounds, TRAINER_ROUNDS);
    ASSERT_TRUE(t.finished);
    ASSERT_EQ_INT(trainer_percent(&t), 100);
    ASSERT_EQ_INT(t.error_count, 0);
}

TEST(koch_records_errors)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, "BI8SYN", 42);
    char wrong[8]; strcpy(wrong, t.target);
    wrong[4] = (wrong[4] == 'K') ? 'M' : 'K';
    ASSERT_TRUE(!trainer_submit(&t, wrong));
    ASSERT_EQ_INT(t.error_count, 1);
    ASSERT_EQ_INT(t.errors[0].expected, t.target[4]);
    ASSERT_EQ_INT(t.errors[0].got, wrong[4]);
    /* 重复提交同一轮不应发生，但要健壮：答案比目标短时，缺位记 '_'。
     * "KM" 长度 2，与 5 字符目标逐位比较：位 0 匹配，位 1..4 不匹配，
     * 其中位 1 有对应字符（记录实际错答），位 2..4 超出答案长度（记录 '_'）。 */
    ASSERT_TRUE(!trainer_submit(&t, "KM"));
    ASSERT_EQ_INT(t.error_count, 5);
    ASSERT_EQ_INT(t.errors[1].expected, t.target[1]);
    ASSERT_EQ_INT(t.errors[1].got, 'M');
    ASSERT_EQ_INT(t.errors[2].expected, t.target[2]);
    ASSERT_EQ_INT(t.errors[2].got, '_');
    ASSERT_EQ_INT(t.errors[3].expected, t.target[3]);
    ASSERT_EQ_INT(t.errors[3].got, '_');
    ASSERT_EQ_INT(t.errors[4].expected, t.target[4]);
    ASSERT_EQ_INT(t.errors[4].got, '_');
}

TEST(error_array_hits_bound)
{
    /* 驱动错误数组到 TRAINER_MAX_ERRORS 上限：Koch 一轮 5 字符，
     * "?????" 与目标（不含 '?'）逐位都不同，4 次提交尝试记录 20 条，
     * 应被截断在 16 条，且不溢出数组。 */
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, "BI8SYN", 42);
    for (int k = 0; k < 4; k++) {
        ASSERT_TRUE(!trainer_submit(&t, "?????"));
    }
    ASSERT_EQ_INT(t.error_count, TRAINER_MAX_ERRORS);
}

TEST(callsign_mode_options_contain_target)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_CALLSIGN, 39, "BI8SYN", 1);
    ASSERT_TRUE(!t.charset_fallback);
    int found = 0;
    for (int i = 0; i < 4; i++) if (strcmp(t.options[i], t.target) == 0) found++;
    ASSERT_EQ_INT(found, 1);
    ASSERT_EQ_STR(t.play_text, t.target);
    ASSERT_TRUE(trainer_submit(&t, t.target));
    trainer_next(&t);
    ASSERT_TRUE(!trainer_submit(&t, "ZZ9ZZ"));
    ASSERT_TRUE(t.error_count >= 1);
}

TEST(callsign_mode_falls_back_when_charset_small)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_CALLSIGN, 1, "BI8SYN", 1);   /* 第 1 课只有 K M */
    ASSERT_TRUE(t.charset_fallback);
    ASSERT_TRUE(strlen(t.target) >= 3);
}

TEST(qso_mode_play_text_and_answer)
{
    /* seed=8 是 RST 选项洗牌第一次迭代抽到 i==j 的种子，
     * 是自交换 UB 防护的回归测试，保留不要换掉。 */
    trainer_t t; trainer_start(&t, TRAINER_MODE_QSO, 39, "BI8SYN", 8);
    char prefix[32]; snprintf(prefix, sizeof prefix, "%s DE ", t.my_callsign);
    ASSERT_TRUE(strstr(t.play_text, prefix) == t.play_text);
    ASSERT_TRUE(strstr(t.play_text, t.target) != NULL);
    ASSERT_TRUE(strstr(t.play_text, t.rst) != NULL);
    size_t len = strlen(t.play_text);
    ASSERT_TRUE(len >= 2);
    ASSERT_EQ_STR(t.play_text + len - 2, "BK");
    char ans[16]; snprintf(ans, sizeof ans, "%s %s", t.target, t.rst);
    ASSERT_TRUE(trainer_submit(&t, ans));
    trainer_next(&t);
    snprintf(ans, sizeof ans, "%s 000", t.target);
    ASSERT_TRUE(!trainer_submit(&t, ans));
}

TEST(qso_worst_case_play_text_no_truncation)
{
    /* seed=3、lesson=39、11 字符操作员呼号（settings 字段 char[12] 的最大内容长度）
     * 恰好抽到一个 6 字符（前缀2+数字1+后缀3）呼号，这是 play_text 需要的最长句子：
     * 11("K1ABCDEFGHI") + " DE " + 6+" "+6 + " K UR RST " + 3+" "+3 + " BK"
     * = 48 字符，需要 49 字节。此测试曾在 play_text[48] 下实测失败（丢失结尾的
     * "K"，句子截断为 "...RST 599 599 B"），扩到 play_text[64] 后通过。 */
    trainer_t t; trainer_start(&t, TRAINER_MODE_QSO, 39, "K1ABCDEFGHI", 3);
    ASSERT_EQ_INT((int)strlen(t.target), 6);   /* 确认确实抽到了最长呼号 */
    size_t len = strlen(t.play_text);
    ASSERT_EQ_STR(t.play_text + len - 2, "BK");
}

TEST(qso_missing_my_callsign_falls_back)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_QSO, 39, NULL, 8);
    ASSERT_EQ_STR(t.my_callsign, "NOCALL");
    ASSERT_TRUE(strstr(t.play_text, "NOCALL DE ") == t.play_text);

    trainer_t t2; trainer_start(&t2, TRAINER_MODE_QSO, 39, "", 8);
    ASSERT_EQ_STR(t2.my_callsign, "NOCALL");
    ASSERT_TRUE(strstr(t2.play_text, "NOCALL DE ") == t2.play_text);
}

int main(void)
{
    RUN(koch_ten_rounds_and_percent);
    RUN(koch_records_errors);
    RUN(error_array_hits_bound);
    RUN(callsign_mode_options_contain_target);
    RUN(callsign_mode_falls_back_when_charset_small);
    RUN(qso_mode_play_text_and_answer);
    RUN(qso_worst_case_play_text_no_truncation);
    RUN(qso_missing_my_callsign_falls_back);
    TEST_MAIN_END();
}
