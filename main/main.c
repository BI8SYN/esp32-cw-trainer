#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "cw_settings.h"
#include "tone_gen.h"
#include "cw_player.h"
#include "key_input.h"
#include "backlight.h"
#include "battery.h"
#include "ui_async.h"
#include "ui_common.h"
#include "ui_home.h"
#include "morse_timing.h"

static const char *TAG = "main";

static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

static void battery_ui(void *arg) { ui_home_set_battery((uint8_t)(uintptr_t)arg); }
static void battery_cb(uint8_t percent, uint16_t mv) { (void)mv; ui_async_post(battery_ui, (void *)(uintptr_t)percent); }

void app_main(void)
{
    esp_log_level_set("XPT2046", ESP_LOG_WARN);
    nvs_init();
    ESP_ERROR_CHECK(cw_settings_init());
    const cw_settings_t *cfg = cw_settings_get();
    ESP_LOGI(TAG, "cfg: wpm=%u/%u tone=%u lesson=%u reset=%d",
             cfg->char_wpm, cfg->eff_wpm, cfg->tone_hz, cfg->koch_lesson, cw_settings_was_reset());

    /* 音频 */
    if (tone_gen_init() == ESP_OK) {
        tone_gen_set_freq(cfg->tone_hz);
        tone_gen_set_volume(cfg->volume);
    }
    morse_timing_t tm;
    morse_timing_compute(cfg->char_wpm, cfg->eff_wpm, &tm);
    ESP_ERROR_CHECK(cw_player_init());
    cw_player_set_timing(&tm);
    ESP_ERROR_CHECK(key_input_init());

    /* 显示 */
    backlight_init();
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    const esp_timer_create_args_t tick_args = { .callback = &lv_tick_task, .name = "lv_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));
    backlight_set(cfg->brightness);

    ui_nav_show(ui_home_create);
    ESP_ERROR_CHECK(battery_start(battery_cb));
    ESP_LOGI(TAG, "ui ready");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5));
        lv_timer_handler();
    }
}
