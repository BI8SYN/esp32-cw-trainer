#include "test_util.h"
#include "keyer.h"

/* 把 keyer 从 t0 运行到 t1（含），返回输出为 true 的毫秒数 */
static int run_count_on(keyer_t *k, uint32_t t0, uint32_t t1)
{
    int on = 0;
    for (uint32_t t = t0; t <= t1; t++) if (keyer_tick(k, t)) on++;
    return on;
}

TEST(straight_key_follows_input)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_STRAIGHT, 60);
    keyer_set_input(&k, true, false);
    ASSERT_TRUE(keyer_tick(&k, 0));
    keyer_set_input(&k, false, false);
    ASSERT_TRUE(!keyer_tick(&k, 1));
}

TEST(straight_key_accepts_either_input)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_STRAIGHT, 60);
    keyer_set_input(&k, false, true);   /* 只接在划线上的直键 */
    ASSERT_TRUE(keyer_tick(&k, 0));
    keyer_set_input(&k, true, true);    /* 两路同时闭合也只持续一次按键 */
    ASSERT_TRUE(keyer_tick(&k, 1));
    keyer_set_input(&k, true, false);   /* 松开其中一路，另一条仍有效 */
    ASSERT_TRUE(keyer_tick(&k, 2));
    keyer_set_input(&k, false, false);
    ASSERT_TRUE(!keyer_tick(&k, 3));
}

TEST(iambic_single_dit_then_gap)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_B, 60);
    keyer_set_input(&k, true, false);
    keyer_tick(&k, 0);
    keyer_set_input(&k, false, false);   /* 立刻松开，仍应发完整 1 个 dit */
    int on = run_count_on(&k, 1, 300);
    ASSERT_EQ_INT(on, 59);   /* t=0..59 共 60ms 为 on，t=0 已在上面 tick 过 */
    ASSERT_TRUE(!k.key_out);
}

TEST(iambic_held_dah_repeats)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_B, 60);
    keyer_set_input(&k, false, true);
    /* dah 180 + gap 60 = 240ms 周期；持续 480ms 应有 2 个 dah = 360ms on */
    int on = run_count_on(&k, 0, 479);
    ASSERT_EQ_INT(on, 360);
}

TEST(iambic_squeeze_alternates)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_A, 60);
    keyer_set_input(&k, true, true);
    /* dit(60)+gap(60)+dah(180)+gap(60) = 360ms 周期，on = 240 */
    int on = run_count_on(&k, 0, 359);
    ASSERT_EQ_INT(on, 240);
    ASSERT_EQ_INT(k.cur_el, 1);   /* 刚发完 dah */
}

TEST(iambic_b_sends_memorized_opposite)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_B, 60);
    keyer_set_input(&k, false, true);      /* 开始 dah */
    run_count_on(&k, 0, 50);
    keyer_set_input(&k, true, true);       /* dah 期间按下 dit */
    run_count_on(&k, 51, 100);
    keyer_set_input(&k, false, false);     /* 全部松开 */
    int on = run_count_on(&k, 101, 600);
    /* 剩余 dah 79ms(101..179) + 记忆 dit 60ms = 139 */
    ASSERT_EQ_INT(on, 139);
}

TEST(iambic_a_does_not_send_memorized)
{
    keyer_t k; keyer_init(&k, KEYER_MODE_IAMBIC_A, 60);
    keyer_set_input(&k, false, true);
    run_count_on(&k, 0, 50);
    keyer_set_input(&k, true, true);
    run_count_on(&k, 51, 100);
    keyer_set_input(&k, false, false);
    int on = run_count_on(&k, 101, 600);
    ASSERT_EQ_INT(on, 79);
}

int main(void)
{
    RUN(straight_key_follows_input);
    RUN(straight_key_accepts_either_input);
    RUN(iambic_single_dit_then_gap);
    RUN(iambic_held_dah_repeats);
    RUN(iambic_squeeze_alternates);
    RUN(iambic_b_sends_memorized_opposite);
    RUN(iambic_a_does_not_send_memorized);
    TEST_MAIN_END();
}
