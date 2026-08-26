#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "cw_settings.h"
#include "tone_gen.h"
#include "cw_player.h"
#include "morse_timing.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

static const char *TAG = "main";
LV_FONT_DECLARE(font20);

static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

/* 开机自检回调：仅用于验证发报计时链是否按预期速率运行（esp_timer 任务上下文）。 */
static void boot_test_cb(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_DONE) {
        ESP_LOGI(TAG, "boot self-test cw done");
    }
}

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

void app_main(void)
{
    esp_log_level_set("XPT2046", ESP_LOG_WARN);
    nvs_init();
    ESP_ERROR_CHECK(cw_settings_init());
    ESP_LOGI(TAG, "cfg: wpm=%u/%u tone=%u lesson=%u reset=%d",
             cw_settings_get()->char_wpm, cw_settings_get()->eff_wpm,
             cw_settings_get()->tone_hz, cw_settings_get()->koch_lesson,
             cw_settings_was_reset());

    if (tone_gen_init() == ESP_OK) {
        tone_gen_set_freq(cw_settings_get()->tone_hz);
        tone_gen_set_volume(cw_settings_get()->volume);
        morse_timing_t tm;
        morse_timing_compute(cw_settings_get()->char_wpm, cw_settings_get()->eff_wpm, &tm);
        ESP_ERROR_CHECK(cw_player_init());
        cw_player_set_timing(&tm);
        cw_player_play("PARIS", boot_test_cb, NULL);
    }

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    const esp_timer_create_args_t tick_args = { .callback = &lv_tick_task, .name = "lv_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label, &font20, 0);
    lv_label_set_text(label, "CW Trainer");
    lv_obj_center(label);
    ESP_LOGI(TAG, "ui ready");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
