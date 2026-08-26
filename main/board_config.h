#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
#include "driver/gpio.h"

#define BOARD_LCD_BL_GPIO        GPIO_NUM_21
#define BOARD_AUDIO_DAC_GPIO     GPIO_NUM_26   /* DAC2，I2S 内置 DAC */
#define BOARD_AUDIO_EN_GPIO      GPIO_NUM_4
#define BOARD_AUDIO_EN_ACTIVE    0             /* 低电平开功放 */
#define BOARD_AUDIO_MUTE_LEVEL   1
#define BOARD_KEY_DIT_GPIO       GPIO_NUM_22   /* CN3 外接口，预留 */
/* CN3 外接口，预留。
 * 已知问题（task-13 设备实测发现，未改动本引脚定义）：
 *   docs/vendor-manual.md:173 与 docs/hardware.md:64 把这个网络标注为
 *   「扩展 SPI 片选预留」，方向是 output，不是 input——也就是说 CN3 丝印上
 *   这条线在板子原本的设计意图里是一个 SPI CS 输出，板上很可能有比 ESP32
 *   内部弱上拉（约 45kΩ）更强的外部下拉/偏置元件来维持它作为 CS 的空闲电平。
 *   实测：把它按 GPIO_MODE_INPUT + GPIO_PULLUP_ENABLE 配置后，CN3 未接任何
 *   东西的情况下，两次独立复位读到的都是低电平，而不是内部上拉应有的高电平。
 *   后果：key_input 外接口模式下把"低电平"当作"划桨按下"（低有效），所以
 *   选中外接口模式的那一刻起，dah 会被读成一直按着，与操作者是否真的碰了
 *   键无关（key_input.c 在选中外接口模式时会对此打一条 ESP_LOGW，但只是
 *   诊断用的日志，不会拒绝启用外接口模式）。
 *   两个候选替代方案，各有代价，都需要拿真板子核实后再选，这里不擅自改：
 *     - CN4 的 IO35：input-only 引脚，适合接键，但 ESP32 的 input-only 引脚
 *       （34~39）完全没有内部上下拉，需要外接电阻——即需要一次硬件改动。
 *     - CN3 的 IO19（SD MISO）：是输入，可以工作，但它在 SD 卡的 SPI 总线
 *       上，一旦以后接入 SD 卡功能会产生总线冲突，静默借用这条线会埋雷。
 *   在上面两者之间做选择需要拿万用表量实际板子、并且对 SD 卡槽的后续规划
 *   做取舍，这两点都不是能在这里凭代码猜出来的，所以维持现状，把已知情况
 *   记在这里——接物理键之前，请先看这条注释。 */
#define BOARD_KEY_DAH_GPIO       GPIO_NUM_27   /* CN3 外接口，预留；空闲电平实测偏低，见上方说明 */
#define BOARD_BATTERY_ADC_CH     ADC1_CHANNEL_6 /* GPIO34 */

#endif
