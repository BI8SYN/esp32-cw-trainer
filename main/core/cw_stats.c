#include "cw_stats.h"
#include "koch_engine.h"
#include <string.h>

/* 计数器封顶而不是回绕：uint16 回绕成 0 会让错误率突然跳成怪值，
   而对使用者来说"考过 65536 次"和"考过很多次"没有区别。 */
#define COUNT_MAX 0xFFFFu

static int idx_of(char c)
{
    const char *p = strchr(KOCH_ORDER, c);
    /* strchr 会匹配到结尾的 '\0'，'\0' 不是有效字符，必须排除 */
    if (!p || !c) return -1;
    int i = (int)(p - KOCH_ORDER);
    return i < CW_STATS_CHARS ? i : -1;
}

static void bump(uint16_t *v)
{
    if (*v < COUNT_MAX) (*v)++;
}

void cw_stats_init(cw_stats_t *s)
{
    memset(s, 0, sizeof *s);
    s->version = CW_STATS_VERSION;
}

void cw_stats_add(cw_stats_t *s, uint8_t lesson, uint8_t pct,
                  const char *shown, const char *wrong)
{
    if (pct > 100) pct = 100;

    for (const char *p = shown; p && *p; p++) {
        int i = idx_of(*p);
        if (i >= 0) bump(&s->seen[i]);
    }
    for (const char *p = wrong; p && *p; p++) {
        int i = idx_of(*p);
        if (i >= 0) bump(&s->err[i]);
    }

    s->hist_pct[s->hist_head] = pct;
    s->hist_lesson[s->hist_head] = lesson;
    s->hist_head = (uint8_t)((s->hist_head + 1) % CW_STATS_HIST);
    if (s->hist_n < CW_STATS_HIST) s->hist_n++;
}

int cw_stats_history(const cw_stats_t *s, uint8_t *pct, uint8_t *lesson, int max_n)
{
    int n = s->hist_n;
    if (n > max_n) n = max_n;
    /* head 指向下一个待写位置，所以最旧的一条在 head - hist_n 处（环形）。
       这里按时间正序输出，画曲线时从左到右直接用。 */
    int start = (s->hist_head - n + CW_STATS_HIST * 2) % CW_STATS_HIST;
    for (int i = 0; i < n; i++) {
        int k = (start + i) % CW_STATS_HIST;
        if (pct) pct[i] = s->hist_pct[k];
        if (lesson) lesson[i] = s->hist_lesson[k];
    }
    return n;
}

uint8_t cw_stats_err_pct(const cw_stats_t *s, char c)
{
    int i = idx_of(c);
    if (i < 0 || s->seen[i] == 0) return 0;
    uint32_t p = (uint32_t)s->err[i] * 100u / s->seen[i];
    return p > 100 ? 100 : (uint8_t)p;
}

int cw_stats_worst(const cw_stats_t *s, char *chars, uint8_t *err_pct, int max_n)
{
    if (max_n <= 0) return 0;

    int n = 0;
    for (int i = 0; i < CW_STATS_CHARS && KOCH_ORDER[i]; i++) {
        if (s->seen[i] == 0 || s->err[i] == 0) continue;   /* 没考过或没错过都不列 */
        uint32_t p = (uint32_t)s->err[i] * 100u / s->seen[i];
        if (p > 100) p = 100;

        /* 插入排序：数据最多 40 条，比引入排序算法划算 */
        int at = n;
        while (at > 0 && err_pct[at - 1] < (uint8_t)p) at--;
        if (at >= max_n) continue;
        int end = n < max_n ? n : max_n - 1;
        for (int k = end; k > at; k--) {
            chars[k] = chars[k - 1];
            err_pct[k] = err_pct[k - 1];
        }
        chars[at] = KOCH_ORDER[i];
        err_pct[at] = (uint8_t)p;
        if (n < max_n) n++;
    }
    return n;
}
