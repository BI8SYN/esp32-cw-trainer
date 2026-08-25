#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

static const char *TAG = "main";
LV_FONT_DECLARE(font20);

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

void app_main(void)
{
    esp_log_level_set("XPT2046", ESP_LOG_WARN);
    nvs_init();
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
