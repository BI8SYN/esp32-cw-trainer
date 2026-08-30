#include "version_cmp.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * 解析出三段数字。返回是否解析成功。
 *
 * 判据是"开头能不能读出至少一段数字"：`v1.5.0` / `1.5` / `v1.5.0-keytest` 都算
 * 成功，`latest` / 空串算失败。缺位补 0，后缀（第一个非数字非点之后的一切）
 * 直接忽略——发布用的版本号一律是 vX.Y.Z，后缀只出现在临时构建上。
 */
static bool parse(const char *s, unsigned out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (!s) return false;
    if (*s == 'v' || *s == 'V') s++;
    if (*s < '0' || *s > '9') return false;

    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') break;
        unsigned n = 0;
        /* 上限保护：版本号不可能大到溢出，但清单是从网络上读来的，
           不能假设它一定是善意的。 */
        while (*s >= '0' && *s <= '9' && n < 100000u) n = n * 10 + (unsigned)(*s++ - '0');
        while (*s >= '0' && *s <= '9') s++;   /* 位数离谱时把剩下的数字吃掉 */
        out[i] = n;
        if (*s != '.') break;
        s++;
    }
    return true;
}

int version_cmp(const char *a, const char *b)
{
    unsigned va[3], vb[3];
    if (!parse(a, va) || !parse(b, vb)) return VERSION_CMP_BAD;
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}
