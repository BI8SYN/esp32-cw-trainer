#include "test_util.h"
#include "version_cmp.h"

TEST(equal)
{
    ASSERT_EQ_INT(version_cmp("v1.5.0", "v1.5.0"), 0);
}

TEST(patch_minor_major)
{
    ASSERT_TRUE(version_cmp("v1.5.1", "v1.5.0") > 0);
    ASSERT_TRUE(version_cmp("v1.5.0", "v1.5.1") < 0);
    ASSERT_TRUE(version_cmp("v1.6.0", "v1.5.9") > 0);
    ASSERT_TRUE(version_cmp("v2.0.0", "v1.9.9") > 0);
}

/*
 * 这一条是换掉 strcmp 的**首要理由**，不是补充。
 * strcmp("v1.10.0", "v1.9.0") < 0——因为 '1' < '9'，v1.10.0 会被判成比 v1.9.0
 * 旧。这个项目迟早会到 v1.10，到那天所有设备都会拒绝升级且毫无提示。
 */
TEST(two_digit_parts_are_not_compared_as_text)
{
    ASSERT_TRUE(version_cmp("v1.10.0", "v1.9.0") > 0);
    ASSERT_TRUE(version_cmp("v1.5.10", "v1.5.9") > 0);
    ASSERT_TRUE(version_cmp("v10.0.0", "v9.0.0") > 0);
}

/* 发布脚本写的都带 v，但清单是手写也能改的，别因为少个字母就判不出来 */
TEST(leading_v_is_optional_on_either_side)
{
    ASSERT_EQ_INT(version_cmp("1.5.0", "v1.5.0"), 0);
    ASSERT_EQ_INT(version_cmp("v1.5.0", "1.5.0"), 0);
}

/*
 * 后缀忽略，只比三段数字。冒烟测试用过 v1.5.0-keytest 这种版本号，
 * 它和已发布的 v1.5.0 应该算同一版，不该互相提示升级。
 */
TEST(suffix_is_ignored)
{
    ASSERT_EQ_INT(version_cmp("v1.5.0-keytest", "v1.5.0"), 0);
    ASSERT_TRUE(version_cmp("v1.5.1-rc1", "v1.5.0") > 0);
}

/* 缺位按 0 补：v1.5 就是 v1.5.0 */
TEST(missing_parts_default_to_zero)
{
    ASSERT_EQ_INT(version_cmp("v1.5", "v1.5.0"), 0);
    ASSERT_TRUE(version_cmp("v1.6", "v1.5.9") > 0);
}

/*
 * 解析不了就说解析不了，**不要退回字符串比较**。
 * 调用方据此选择「不主动提示升级」——把一个看不懂的版本号猜成"更新"，
 * 换来的是把用户静默降级到一个不知道是什么的固件。
 */
TEST(unparseable_is_reported_not_guessed)
{
    ASSERT_EQ_INT(version_cmp("", "v1.5.0"), VERSION_CMP_BAD);
    ASSERT_EQ_INT(version_cmp("v1.5.0", ""), VERSION_CMP_BAD);
    ASSERT_EQ_INT(version_cmp("latest", "v1.5.0"), VERSION_CMP_BAD);
    ASSERT_EQ_INT(version_cmp("v1.5.0", NULL), VERSION_CMP_BAD);
    ASSERT_EQ_INT(version_cmp(NULL, NULL), VERSION_CMP_BAD);
}

/* 设备在跑一个比服务器还新的版本——正是本次要修的那个场景 */
TEST(device_newer_than_server)
{
    ASSERT_TRUE(version_cmp("v1.4.4", "v1.5.0") < 0);
}

int main(void)
{
    RUN(equal);
    RUN(patch_minor_major);
    RUN(two_digit_parts_are_not_compared_as_text);
    RUN(leading_v_is_optional_on_either_side);
    RUN(suffix_is_ignored);
    RUN(missing_parts_default_to_zero);
    RUN(unparseable_is_reported_not_guessed);
    RUN(device_newer_than_server);
    TEST_MAIN_END();
}
