#include "test_util.h"
#include "koch_engine.h"
#include <string.h>

TEST(charset_grows_with_lesson)
{
    char cs[48];
    ASSERT_EQ_INT(koch_charset(1, cs), 2);
    ASSERT_EQ_STR(cs, "KM");
    ASSERT_EQ_INT(koch_charset(7, cs), 8);
    ASSERT_EQ_STR(cs, "KMRSUAPT");
    ASSERT_EQ_INT(koch_charset(39, cs), 40);
    ASSERT_EQ_INT(koch_charset(0, cs), 2);     /* 钳到 1 */
    ASSERT_EQ_INT(koch_charset(99, cs), 40);   /* 钳到 39 */
}

TEST(new_char)
{
    ASSERT_EQ_INT(koch_new_char(1), 'M');
    ASSERT_EQ_INT(koch_new_char(7), 'T');
}

TEST(group_uses_charset_and_contains_new_char)
{
    uint8_t lessons[] = {1, 7, 39, 0, 40, 255};
    for (size_t l = 0; l < sizeof(lessons) / sizeof(lessons[0]); l++) {
        uint8_t lesson = lessons[l];
        rng_t r; rng_seed(&r, 12345);
        char cs[48];
        koch_charset(lesson, cs);
        char new_ch = koch_new_char(lesson);

        for (int i = 0; i < 30; i++) {
            char g[8]; koch_make_group(lesson, &r, g);
            ASSERT_EQ_INT((int)strlen(g), 5);
            ASSERT_TRUE(strchr(g, new_ch) != NULL);
            for (int j = 0; j < 5; j++) ASSERT_TRUE(strchr(cs, g[j]) != NULL);
        }
    }
}

TEST(rng_edge_cases)
{
    rng_t r;
    rng_seed(&r, 0);
    uint32_t v1 = rng_next(&r);
    uint32_t v2 = rng_next(&r);
    ASSERT_TRUE(v1 != 0);
    ASSERT_TRUE(v2 != 0);
    ASSERT_TRUE(v1 != v2);

    uint32_t zero_result = rng_below(&r, 0);
    ASSERT_EQ_INT(zero_result, 0);
}

TEST(pass_threshold)
{
    ASSERT_TRUE(koch_passed(90));
    ASSERT_TRUE(koch_passed(100));
    ASSERT_TRUE(!koch_passed(89));
}

TEST(score_group)
{
    uint8_t mm;
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KMRSU", &mm), 5);
    ASSERT_EQ_INT(mm, 0);
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KMRSA", &mm), 4);
    ASSERT_EQ_INT(mm, 0x10);
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KM", &mm), 2);   /* 短答案缺位算错 */
    ASSERT_EQ_INT(mm, 0x1C);
    ASSERT_EQ_INT(koch_score_group("KMRSU", "KMRSUXX", &mm), 5);  /* 长答案，多余字符被忽略 */
    ASSERT_EQ_INT(mm, 0);
}

int main(void)
{
    RUN(charset_grows_with_lesson);
    RUN(new_char);
    RUN(group_uses_charset_and_contains_new_char);
    RUN(rng_edge_cases);
    RUN(pass_threshold);
    RUN(score_group);
    TEST_MAIN_END();
}
