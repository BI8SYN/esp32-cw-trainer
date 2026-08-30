#ifndef VERSION_CMP_H
#define VERSION_CMP_H
#include <limits.h>

/* 无法解析（空串、NULL、开头就不是数字）。用一个不可能与真实比较结果混淆的
   哨兵值，而不是 0——0 的意思是"两边一样新"，把"看不懂"和"一样新"混为一谈会
   让调用方无法区分该不该提示。 */
#define VERSION_CMP_BAD INT_MIN

/*
 * 比较两个 `vMAJOR.MINOR.PATCH` 版本号，返回 <0 / 0 / >0，语义同 strcmp。
 *
 * 接受 `v1.5.0`、`1.5.0`（v 可有可无）、`v1.5`（缺位补 0）、`v1.5.0-keytest`
 * （后缀忽略，只比三段数字）。
 *
 * **不要用 strcmp 代替它。** `strcmp("v1.10.0", "v1.9.0") < 0`——'1' < '9'，
 * v1.10.0 会被判成比 v1.9.0 旧。这个工程迟早会到 v1.10，到那天所有设备都会
 * 拒绝升级，而且不会有任何报错。
 */
int version_cmp(const char *a, const char *b);

#endif
