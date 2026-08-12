# 硬件参考 — ESP32-2432S0248R-PLUS（CYD 2.8" 电阻触摸版）

整理自 `docs/esp32伴侣盒子开发文档.pdf` 与实测（`esptool flash_id`）。

## 芯片与板卡

| 项目 | 值 |
|---|---|
| 模组 | ESP32-WROOM-32E |
| 芯片 | ESP32-D0WD-V3 (revision v3.1)，Xtensa 双核 LX6 @240MHz |
| Flash | 4MB（实测 Manufacturer 46 / Device 4016），3.3V（由 strapping 引脚决定） |
| SRAM / RTC SRAM | 520KB / 16KB，**无 PSRAM** |
| 本机 MAC | `8c:94:df:72:7b:24` |
| 晶振 | 40MHz |
| 屏幕 | 2.8" TFT，240×320，ST7789 驱动，**BGR 色序** |
| 触摸 | 电阻式，XPT2046（部分资料写作 XP2046） |
| 板卡尺寸 | 86mm × 50mm |
| 逻辑电平 | 3.3V |
| USB 串口 | CH340（macOS 下为 `/dev/cu.usbserial-310`） |

## GPIO 映射

### LCD / ST7789（VSPI）

| 信号 | GPIO | 方向 | 说明 |
|---|---|---|---|
| LCD_SCK | 14 | 输出 | SPI 时钟 |
| LCD_MISO | 12 | 输入 | 默认不读屏 |
| LCD_MOSI | 13 | 输出 | |
| LCD_RS / DC | 2 | 输出 | 数据/命令选择 |
| LCD_CS | 15 | 输出 | |
| LCD_BL | 21 | 输出 | 背光，原厂固件用 LEDC PWM 调光 |
| LCD_RST | — | — | 接 ESP32 EN，不占用 GPIO（驱动里配 `-1`） |

### 触摸 XPT2046（独立 SPI，非 VSPI 复用）

| 信号 | GPIO | 方向 |
|---|---|---|
| RTP_IRQ (SENSOR_VP) | 36 | 输入（仅输入脚） |
| RTP_DOUT / MISO (SENSOR_VN) | 39 | 输入（仅输入脚） |
| RTP_DIN / MOSI | 32 | 输出 |
| RTP_CS | 33 | 输出 |
| RTP_SCK | 25 | 输出 |

### SD 卡（HSPI，预留）

| 信号 | GPIO |
|---|---|
| SD_MOSI | 23 |
| SD_MISO | 19 |
| SD_SCK | 18 |
| SD_CS | 5 |

### 其它

| 功能 | GPIO | 说明 |
|---|---|---|
| 电池电压 ADC | 34 | 仅输入脚，ADC1；原厂固件做了 eFuse 校准 + 分压补偿 |
| RGB LED — R | 17 | 板载三色灯（共阳，低电平点亮） |
| RGB LED — G | 22 | |
| RGB LED — B | 16 | |
| AUDIO_IN | 26 | 输入/ADC；同时是 DAC2 输出脚，原厂固件用 I2S 内置 DAC 输出音频 |
| AUDIO_EN | 4 | 输出，音频/功放使能 |
| 扩展 SPI CS | 27 | 预留 |

## 接口

| 接口 | 位置 | 说明 |
|---|---|---|
| USB Type-C | 侧边 | 5V 供电 + CH340 串口下载 |
| 1.25P 串口 | 背面 CN2 | UART 调试（RXD/TXD/GND） |
| 1.25P 电池口 | 背面 CN5 | **左正右负**，3.3V 输入；严禁反接或接 5V |
| 2.0P 音频口 | 正面 CN1 | 音频输出（配合 GPIO26 DAC + GPIO4 使能） |
| 外置扩展口 | CN3 | IO27(CS) / IO18(SCK) / IO19(MISO) / IO23(MOSI) / IO22 |
| 扩展口 | CN4 | GND / IO35 / 3.3V |
| TF 卡座 | 正面 TF1 | MicroSD |
| BOOT / RESET | SW1 / SW2 | 板载按键 |

## 本机改装

盒子内部除开发板外还加了（见 `docs/IMG_1302.jpeg`）：

- 18650 锂电池 1 节
- 升压/充放电模块（板上有 1R0 电感，输出接到开发板电池口）
- 绿色 3D 打印外壳，侧面开有拨动开关孔位

## 注意事项

- GPIO34 / 36 / 39 为**仅输入**引脚，不能做输出。
- 屏幕若红蓝反色，检查 ST7789 的 BGR/RGB 初始化位。
- 屏幕旋转或坐标方向改变后需要重新做触摸校准。
- 板卡逻辑 3.3V，勿直接接 5V 信号。

## 资料来源

厂商资料下载页：https://www.zlxchina.xyz/
