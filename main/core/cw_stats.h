#ifndef CW_STATS_H
#define CW_STATS_H
#include <stdbool.h>
#include <stdint.h>

/*
 * 训练统计：每个字母的抄错率，以及最近若干次结算的正确率。
 *
 * 纯 C、不依赖 ESP-IDF——本文件在 main/core/ 下，会被 test/Makefile 自动编译
 * 进每一个主机测试。落盘由 main/stats_store.c 负责。
 *
 * 存在独立的 NVS key，**不进 cw_settings_t**：那个结构体一旦长度或版本变化
 * 就 set_defaults()，会清空用户全部 Koch 进度。详见 net_store.h 的同一段说明。
 */

/* KOCH_ORDER 是 40 个字符（见 koch_engine.c） */
#define CW_STATS_CHARS   40
/* 保留多少次结算用于画进度曲线。32 条在 320px 宽上每条 10px，正好画得开。 */
#define CW_STATS_HIST    32
#define CW_STATS_VERSION 1

typedef struct {
    uint32_t version;
    uint16_t seen[CW_STATS_CHARS];   /* 该字母被考过多少次 */
    uint16_t err[CW_STATS_CHARS];    /* 其中抄错多少次 */
    uint8_t  hist_pct[CW_STATS_HIST];
    uint8_t  hist_lesson[CW_STATS_HIST];
    uint8_t  hist_n;                 /* 已记录条数，<= CW_STATS_HIST */
    uint8_t  hist_head;              /* 环形写入位置 */
} cw_stats_t;

void cw_stats_init(cw_stats_t *s);

/*
 * 记录一次结算。
 *   shown —— 本轮出现过的全部字符（用于 seen 计数）
 *   wrong —— 其中被抄错的字符（期望值，不是用户答的）
 * 两个都是普通字符串；非 Koch 字符（小写、标点）静默忽略。
 */
void cw_stats_add(cw_stats_t *s, uint8_t lesson, uint8_t pct,
                  const char *shown, const char *wrong);

/* 按时间正序取历史，返回条数。 */
int cw_stats_history(const cw_stats_t *s, uint8_t *pct, uint8_t *lesson, int max_n);

/* 某字母的抄错率（0~100）。没考过或没错过都返回 0。 */
uint8_t cw_stats_err_pct(const cw_stats_t *s, char c);

/* 抄错率最高的若干个字母，按错误率降序。只列出真的错过的，返回条数。 */
int cw_stats_worst(const cw_stats_t *s, char *chars, uint8_t *err_pct, int max_n);

#endif
