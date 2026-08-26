#include "battery.h"
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

static esp_adc_cal_characteristics_t s_cal;
static battery_cb_t s_cb;
static uint8_t s_percent = 100;

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
        if (s_cb) s_cb(s_percent, (uint16_t)mv);
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

esp_err_t battery_start(battery_cb_t cb)
{
    s_cb = cb;
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_12));
    ESP_ERROR_CHECK(adc1_config_channel_atten(BOARD_BATTERY_ADC_CH, ADC_ATTEN_DB_11));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &s_cal);
    return xTaskCreate(task, "battery", 3072, NULL, 1, NULL) == pdPASS ? ESP_OK : ESP_FAIL;
}

uint8_t battery_percent(void) { return s_percent; }
