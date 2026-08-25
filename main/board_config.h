#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
#include "driver/gpio.h"

#define BOARD_LCD_BL_GPIO        GPIO_NUM_21
#define BOARD_AUDIO_DAC_GPIO     GPIO_NUM_26   /* DAC2，I2S 内置 DAC */
#define BOARD_AUDIO_EN_GPIO      GPIO_NUM_4
#define BOARD_AUDIO_EN_ACTIVE    0             /* 低电平开功放 */
#define BOARD_AUDIO_MUTE_LEVEL   1
#define BOARD_KEY_DIT_GPIO       GPIO_NUM_22   /* CN3 外接口，预留 */
#define BOARD_KEY_DAH_GPIO       GPIO_NUM_27   /* CN3 外接口，预留 */
#define BOARD_BATTERY_ADC_CH     ADC1_CHANNEL_6 /* GPIO34 */

#endif
