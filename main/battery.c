#include "battery.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "board_config.h"

static const char *TAG = "battery";
#define SAMPLES    16
#define SCALE_PERMILLE 2000   /* 1:1 分压，与 fmo-companion 一致 */
#define EMPTY_MV   3000
#define FULL_MV    4180
#define PERIOD_MS  10000
/* 与 fmo-companion/main/battery_monitor.c 里的 BATTERY_PROTECT_PERCENT 保持一致
 * （spec 里"<5% 沿用现有关机"用的也是这个数字）。这里只打日志、不做任何关机/
 * 保护动作——见本文件顶部关于"为什么不在这里自动关机"的说明（README 与
 * spec 文档里也各记了一笔）。 */
#define CRITICAL_PERCENT 5

static esp_adc_cal_characteristics_t s_cal;
static battery_cb_t s_cb;
static uint8_t s_percent = 100;
static bool s_was_critical;   /* 只在跌破/回升穿越阈值时打一条日志，不是每 10s 刷屏 */

static uint8_t to_percent(uint32_t mv)
{
    if (mv <= EMPTY_MV) return 0;
    if (mv >= FULL_MV) return 100;
    return (uint8_t)((mv - EMPTY_MV) * 100 / (FULL_MV - EMPTY_MV));
}

static void task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t sum = 0;
        for (int i = 0; i < SAMPLES; i++) sum += (uint32_t)adc1_get_raw(BOARD_BATTERY_ADC_CH);
        uint32_t adc_mv = esp_adc_cal_raw_to_voltage(sum / SAMPLES, &s_cal);
        uint32_t mv = adc_mv * SCALE_PERMILLE / 1000;
        s_percent = to_percent(mv);
        ESP_LOGI(TAG, "%lu mV, %u%%", (unsigned long)mv, s_percent);
        /* spec 里写了"沿用现有低电量逻辑，<5% 关机"，但这个固件从来没有实现过
         * 自动关机——协调者已决定不补：这里既没有真电池能测，也没有人验证过
         * 误触发的后果，一个没测过的自动关机比完全不做更危险；这块电路很可能
         * 本身就有硬件级的欠压保护兜底。这里只加一条跌破阈值时的告警日志，让
         * 这件事至少是可观察的，而不是既没做、也没人知道没做。 */
        bool critical = s_percent < CRITICAL_PERCENT;
        if (critical && !s_was_critical) {
            ESP_LOGW(TAG, "battery critical: %u%% (< %u%%) - no automatic shutdown implemented, "
                     "see README.md 尚未在真实设备上验证的事项", s_percent, CRITICAL_PERCENT);
        }
        s_was_critical = critical;
        if (s_cb) s_cb(s_percent, (uint16_t)mv);
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

esp_err_t battery_start(battery_cb_t cb)
{
    s_cb = cb;
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
    ESP_ERROR_CHECK(adc1_config_channel_atten(BOARD_BATTERY_ADC_CH, ADC_ATTEN_DB_11));

    /* 记录校准来源（不属于 brief 原文，经协调者批准追加）：
     * esp_adc_cal_characterize 的返回值指出实际使用的校准数据来源。
     * 若芯片没有烧录 eFuse 校准值，会静默退化为下面写死的 1100mV 默认参考电压，
     * 所有电压读数都会因此产生系统性偏差且没有任何提示——这里把来源打到日志里，
     * 换一块没有 eFuse 校准的芯片时能立刻看出来，而不是被当成正常读数。 */
    esp_adc_cal_value_t cal_src = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &s_cal);
    switch (cal_src) {
    case ESP_ADC_CAL_VAL_EFUSE_VREF:
        ESP_LOGI(TAG, "adc cal source: eFuse Vref");
        break;
    case ESP_ADC_CAL_VAL_EFUSE_TP:
        ESP_LOGI(TAG, "adc cal source: eFuse Two Point");
        break;
    case ESP_ADC_CAL_VAL_EFUSE_TP_FIT:
        ESP_LOGI(TAG, "adc cal source: eFuse Two Point (fitting curve)");
        break;
    default:
        ESP_LOGW(TAG, "adc cal source: DEFAULT VREF FALLBACK (1100 mV assumed) - readings may be inaccurate");
        break;
    }

    return xTaskCreate(task, "battery", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

uint8_t battery_percent(void) { return s_percent; }
