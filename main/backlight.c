#include "backlight.h"
#include "driver/ledc.h"
#include "board_config.h"

#define BL_TIMER   LEDC_TIMER_0
#define BL_MODE    LEDC_LOW_SPEED_MODE
#define BL_CH      LEDC_CHANNEL_0
#define BL_RES     LEDC_TIMER_10_BIT
#define BL_MAX     ((1U << 10) - 1U)

void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = BL_MODE, .timer_num = BL_TIMER, .duty_resolution = BL_RES,
        .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = BOARD_LCD_BL_GPIO, .speed_mode = BL_MODE, .channel = BL_CH,
        .timer_sel = BL_TIMER, .duty = BL_MAX, .hpoint = 0,
    };
    ledc_channel_config(&c);
}

void backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    ledc_set_duty(BL_MODE, BL_CH, BL_MAX * percent / 100);
    ledc_update_duty(BL_MODE, BL_CH);
}
