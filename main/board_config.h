#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
#include "driver/gpio.h"

#define BOARD_LCD_BL_GPIO        GPIO_NUM_21
#define BOARD_AUDIO_DAC_GPIO     GPIO_NUM_26   /* DAC2，I2S 内置 DAC */
#define BOARD_AUDIO_EN_GPIO      GPIO_NUM_4
#define BOARD_AUDIO_EN_ACTIVE    0             /* 低电平开功放 */
#define BOARD_AUDIO_MUTE_LEVEL   1
/*
 * 外接电键。CN3 丝印顺序是 IO27(CS) IO18(SCK) IO19(MISO) IO23(MOSI) IO22，
 * 所以点在第 2 脚、划在第 4 脚；地从 CN4 第 1 脚引（CN3 没有电源和地）。
 * 低电平有效、内部上拉，按键直接对地即可，不需要任何外接元件。
 * 3.5mm TRS 成品电键的行业约定 Tip=点 / Ring=划 / Sleeve=地 与此天然对齐。
 *
 * 为什么是 18 和 23：板子把这两根规划成 SD 的 SCK/MOSI，也就是 ESP32 往外送的
 * 输出，卡座那侧只是接收端——板上没有任何器件会跟输入抢电平。相比之下
 * 19(SD_MISO) 是回读线，插了 TF 卡时卡片的 DO 会驱动它；35 虽然在 CN4 上，
 * 但属于 34~39 输入专用段，没有内部上下拉，必须外接电阻；22 是板载 RGB 绿灯
 * 通道，线上挂着 LED，空闲电平会被拉进不确定区间。
 *
 * GPIO27 不能用：本机改装时它上面接了蜂鸣器（见 docs/hardware.md「本机改装」），
 * 空闲电平恒为低。此前这里用 27 做划线，一启用外接口就是「划桨一直按着」，
 * 与操作者是否碰过键无关。旧注释推测原因是板载扩展 SPI 片选的外部偏置在拉，
 * 那个推测是错的，别再照着它去量板子。
 */
#define BOARD_KEY_DIT_GPIO       GPIO_NUM_18
#define BOARD_KEY_DAH_GPIO       GPIO_NUM_23
#define BOARD_BATTERY_ADC_CH     ADC1_CHANNEL_6 /* GPIO34 */

#endif
