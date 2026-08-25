idf_component_register(
    SRCS
        "main.c"
        "fonts/font20.c"
        "fonts/font28.c"
        "fonts/EN_BIG.c"
    INCLUDE_DIRS "." "fonts" "core" "ui"
    REQUIRES
        driver
        esp_driver_i2s
        esp_timer
        nvs_flash
        esp_adc
        lvgl
        lv_porting
        lvgl_esp32_drivers
)
