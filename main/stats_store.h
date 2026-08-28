#ifndef STATS_STORE_H
#define STATS_STORE_H
#include "esp_err.h"
#include "cw_stats.h"
#include "trainer.h"

/*
 * cw_stats 的落盘层。统计逻辑本身在 core/cw_stats.c（纯 C、主机可测），
 * 这里只负责 NVS。
 *
 * 与 net_store 同样的道理：存在独立的 NVS key，**不进 cw_settings_t**。
 * 那个结构体一旦长度或版本变化就 set_defaults()，会清空用户的 Koch 进度。
 */

esp_err_t   stats_store_init(void);
cw_stats_t *stats_store_get(void);
esp_err_t   stats_store_save(void);
/* 一轮结束时调用：从 trainer 里取出正确率、出现过的字符和错字，记账并落盘。 */
void        stats_store_record(const trainer_t *t);
/* 清空统计（设置页的恢复出厂设置会连带调用）。 */
esp_err_t   stats_store_reset(void);

#endif
