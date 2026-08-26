#include "test_util.h"
#include "trainer.h"
#include <string.h>

TEST(koch_ten_rounds_and_percent)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, 42);
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
    trainer_t t; trainer_start(&t, TRAINER_MODE_KOCH, 7, 42);
    char wrong[8]; strcpy(wrong, t.target);
    wrong[4] = (wrong[4] == 'K') ? 'M' : 'K';
    ASSERT_TRUE(!trainer_submit(&t, wrong));
    ASSERT_EQ_INT(t.error_count, 1);
    ASSERT_EQ_INT(t.errors[0].expected, t.target[4]);
    ASSERT_EQ_INT(t.errors[0].got, wrong[4]);
    ASSERT_TRUE(!trainer_submit(&t, "KM"));        /* 重复提交同一轮不应发生，但要健壮：缺位记 '_' */
}

TEST(callsign_mode_options_contain_target)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_CALLSIGN, 39, 1);
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
    trainer_t t; trainer_start(&t, TRAINER_MODE_CALLSIGN, 1, 1);   /* 第 1 课只有 K M */
    ASSERT_TRUE(t.charset_fallback);
    ASSERT_TRUE(strlen(t.target) >= 3);
}

TEST(qso_mode_play_text_and_answer)
{
    trainer_t t; trainer_start(&t, TRAINER_MODE_QSO, 39, 8);
    ASSERT_TRUE(strstr(t.play_text, "CQ CQ DE ") == t.play_text);
    ASSERT_TRUE(strstr(t.play_text, t.target) != NULL);
    ASSERT_TRUE(strstr(t.play_text, t.rst) != NULL);
    ASSERT_TRUE(strstr(t.play_text, " BK") != NULL);
    char ans[16]; snprintf(ans, sizeof ans, "%s %s", t.target, t.rst);
    ASSERT_TRUE(trainer_submit(&t, ans));
    trainer_next(&t);
    snprintf(ans, sizeof ans, "%s 000", t.target);
    ASSERT_TRUE(!trainer_submit(&t, ans));
}

int main(void)
{
    RUN(koch_ten_rounds_and_percent);
    RUN(koch_records_errors);
    RUN(callsign_mode_options_contain_target);
    RUN(callsign_mode_falls_back_when_charset_small);
    RUN(qso_mode_play_text_and_answer);
    TEST_MAIN_END();
}
