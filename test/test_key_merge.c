#include "test_util.h"
#include "key_merge.h"

/* 便于书写：不带门闩问题的常见情形，两根线都已开闸 */
static const bool ARMED[2]   = { true, true };
static const bool NOTHING[2] = { false, false };

TEST(touch_only)
{
    const bool touch[2] = { true, false };
    key_merge_out_t m;
    key_merge(touch, NOTHING, ARMED, false, &m);
    ASSERT_TRUE(m.dit);
    ASSERT_TRUE(!m.dah);
    ASSERT_TRUE(m.in[0]);
    ASSERT_TRUE(!m.in[1]);
}

TEST(ext_only)
{
    const bool ext[2] = { false, true };
    key_merge_out_t m;
    key_merge(NOTHING, ext, ARMED, false, &m);
    ASSERT_TRUE(!m.dit);
    ASSERT_TRUE(m.dah);
    ASSERT_TRUE(!m.in[0]);
    ASSERT_TRUE(m.in[1]);
}

/* 并联的全部意义：两路谁按都算，不需要先在菜单里选一个 */
TEST(touch_and_ext_are_ored)
{
    const bool touch[2] = { true, false };
    const bool ext[2]   = { false, true };
    key_merge_out_t m;
    key_merge(touch, ext, ARMED, false, &m);
    ASSERT_TRUE(m.dit);
    ASSERT_TRUE(m.dah);
}

TEST(same_side_from_both_sources_is_not_double_counted)
{
    const bool both[2] = { true, false };
    key_merge_out_t m;
    key_merge(both, both, ARMED, false, &m);
    ASSERT_TRUE(m.dit);
    ASSERT_TRUE(!m.dah);
}

/*
 * 门闩：没开闸的线说什么都不算。
 *
 * 这一条挡的是两种常规工况，不是罕见故障：本机 GPIO27 上接了蜂鸣器（空闲恒为
 * 低），以及成品直键的 TS 单声道插头插进立体声母座时套筒把 Ring 和 Sleeve 短接
 * ——后者每插一次直键都会发生。
 */
TEST(unarmed_ext_line_is_ignored)
{
    const bool ext[2]      = { false, true };   /* 划线一直读成按下 */
    const bool armed[2]    = { true, false };   /* 但它从没被观察到松开过 */
    key_merge_out_t m;
    key_merge(NOTHING, ext, armed, false, &m);
    ASSERT_TRUE(!m.dah);
    ASSERT_TRUE(!m.in[1]);
}

/* 门闩只管外接线，不能连触摸一起挡掉——否则一根坏线会让整台机器没法用 */
TEST(unarmed_line_does_not_block_touch_on_same_side)
{
    const bool touch[2] = { false, true };
    const bool ext[2]   = { false, true };
    const bool armed[2] = { true, false };
    key_merge_out_t m;
    key_merge(touch, ext, armed, false, &m);
    ASSERT_TRUE(m.dah);
    ASSERT_TRUE(m.in[1]);
}

TEST(swap_exchanges_dit_and_dah)
{
    const bool touch[2] = { true, false };
    key_merge_out_t m;
    key_merge(touch, NOTHING, ARMED, true, &m);
    ASSERT_TRUE(!m.dit);
    ASSERT_TRUE(m.dah);
}

/*
 * in[] 是「屏幕上第 i 块」的状态，取在对调之前——UI 高亮要的正是这个量。
 * 若 in[] 也跟着对调，开了对调之后按左块会高亮右块。
 */
TEST(swap_does_not_move_screen_block_state)
{
    const bool touch[2] = { true, false };
    key_merge_out_t m;
    key_merge(touch, NOTHING, ARMED, true, &m);
    ASSERT_TRUE(m.in[0]);
    ASSERT_TRUE(!m.in[1]);
}

TEST(swap_applies_to_external_too)
{
    const bool ext[2] = { true, false };   /* IO18 被按下 */
    key_merge_out_t m;
    key_merge(NOTHING, ext, ARMED, true, &m);
    ASSERT_TRUE(!m.dit);
    ASSERT_TRUE(m.dah);
    ASSERT_TRUE(m.in[0]);   /* 屏幕左块仍然是「被按下的那一块」 */
}

TEST(nothing_pressed_is_silent)
{
    key_merge_out_t m;
    key_merge(NOTHING, NOTHING, ARMED, false, &m);
    ASSERT_TRUE(!m.dit);
    ASSERT_TRUE(!m.dah);
    ASSERT_TRUE(!m.in[0]);
    ASSERT_TRUE(!m.in[1]);
}

int main(void)
{
    RUN(touch_only);
    RUN(ext_only);
    RUN(touch_and_ext_are_ored);
    RUN(same_side_from_both_sources_is_not_double_counted);
    RUN(unarmed_ext_line_is_ignored);
    RUN(unarmed_line_does_not_block_touch_on_same_side);
    RUN(swap_exchanges_dit_and_dah);
    RUN(swap_does_not_move_screen_block_state);
    RUN(swap_applies_to_external_too);
    RUN(nothing_pressed_is_silent);
    TEST_MAIN_END();
}
