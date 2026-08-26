# CW Trainer

ESP32-2432S0248R 上的 CW（莫尔斯电码）练习机固件。设计见 `docs/superpowers/specs/2026-08-25-cw-trainer-design.md`。

## 构建 / 烧录

```bash
PROJECT=firmware/cw-trainer tools/idf.sh build
PROJECT=firmware/cw-trainer tools/idf.sh flash monitor
```

NVS 命名空间 `cwtrn`，与 fmo-companion 互不影响，两个固件可以来回刷而不必互相清空对方的设置
（参见 `docs/build-setup.md`「多固件开发」一节里"NVS 是共享的，不同命名空间不会互相污染"的说明）。

串口：`tools/idf.sh`（以及根 `README.md`）里写的默认口 `/dev/cu.usbserial-310` **不是一个通用值，只是某一台开发机上的实测结果**，换一台机器、换一根线基本都会不一样。用前先确认自己机器上的实际设备名：

```bash
ls /dev/cu.usb*
```

再通过环境变量覆盖（工具脚本本身已经支持，不需要改脚本里的默认值）：

```bash
ESP_PORT=/dev/cu.usbserial-XXXX PROJECT=firmware/cw-trainer tools/idf.sh build
ESP_PORT=/dev/cu.usbserial-XXXX PROJECT=firmware/cw-trainer tools/idf.sh flash monitor
```

## 主机测试

```bash
make -C firmware/cw-trainer/test
```

`main/core/` 下的模块不依赖 ESP-IDF，可在 macOS 直接编译测试。目前 7 个测试程序、41 个用例，全部通过、编译无警告。

## 结构

- `main/core/` 莫尔斯表、时序、电键状态机、解码器、Koch 引擎、呼号生成、练习会话——纯 C，不含任何 ESP-IDF 依赖，这也是它们能在主机上直接跑测试的原因
- `main/*.c` 侧音（I2S 内置 DAC）、文本发报、电键输入、NVS 设置、电量、背光
- `main/ui/` LVGL 页面：主页、课前预告、练习（Koch 听写 / 呼号抄收 / 模拟 QSO 三种模式）、结算、电键、设置，阶段 1 为灰阶版（阶段 2 视觉风格另开计划）

## 侧音采样率：为什么是 32 kHz，不要往下调

`main/tone_gen.c` 里 `SAMPLE_RATE` 定的是 32000，这不是随手选的一个"够用"的数字，而是一条硬下限之上的安全值。

ESP32 的内置 DAC 走的是 legacy I2S 驱动（`driver/i2s.h`），它在设置采样率时会算一个时钟分频值（`mclk_div`），这个值必须小于 256——采样率越低，分频越大，大约在 19.5 kHz 以下就会超过这个上限。麻烦的是驱动内部算这个值的函数（`i2s_calculate_clock`）返回的是 `esp_err_t`，但调用它的 `i2s_set_clock_legacy()` 把返回值直接丢弃、不做任何判断（ESP-IDF v5.5.4 `components/driver/deprecated/i2s_legacy.c`），所以就算分频算超了，`i2s_driver_install()` 依然会返回 `ESP_OK`——**没有任何报错，编译和启动看起来都一切正常**。唯一能看出问题的地方是耳朵：音调完全不对。

也就是说，如果以后想把这个采样率往下调来省 CPU，务必先确认新值离 19.5 kHz 这条线有多远；本项目选 32 kHz 就是为了在这条线之上留出足够余量。

## 外接电键

设置 → 电键 → 电键输入 = 外接口。接线：CN3 IO22 = 点（直键也用此线），IO27 = 划，GND 公共；低电平有效，内部上拉。

**IO27 目前不适合真正接电键的"划"输入，先别指望外接口模式能用**：厂商说明书（`docs/vendor-manual.md`、`docs/hardware.md`）把这个网络标注为"扩展 SPI 片选"，方向是**输出**，不是输入；实测把它配置成输入 + 内部上拉后，板子在没有接任何东西的情况下，两次独立复位读到的都是低电平，而不是内部上拉应有的高电平。后果是：一旦选中外接口模式，"划"从一开始就会被当成"一直按着"，与操作者是否真的碰了键无关。固件在选中外接口模式时会打一条 `ESP_LOGW` 警告（`key_input.c`），但只是诊断用途，不会拒绝启用外接口模式。

这个引脚定义目前**没有改**，因为换一个引脚需要拿万用表在真板子上核实、并且要对 SD 卡槽会不会用做一个取舍，这两点都不是看代码能猜出来的。已知的两个候选、各自的代价：

- **CN4 的 IO35**：input-only 引脚，电气上适合接键；但 ESP32 的 input-only 引脚（34~39）完全没有内部上下拉，需要外接一颗电阻，即需要一次硬件改动。
- **CN3 的 IO19**（SD 卡 MISO）：是输入，电气上可以工作；但它在 SD 卡的 SPI 总线上，一旦以后接入 SD 卡功能，会和 SD 卡的通信产生总线冲突。

在换真实电键之前，请先看 `main/board_config.h` 里针对 `BOARD_KEY_DAH_GPIO` 的完整说明。

## 界面里的中文字体

`main/ui/ui_common.h` 声明了三款字体：`font20`（`UI_FONT_CN`）、`font28`（`UI_FONT_MID`）、`EN_BIG`（`UI_FONT_BIG`）。**只有 `font20` 含中文字形**，`font28` 和 `EN_BIG` 只是可打印 ASCII 的位图集。

任何含中文的 label 如果被设成 `font28` 或 `EN_BIG`，不会有编译错误、不会有运行时警告、LVGL 的控件树也完全正确——那段文字只是渲染成空白，只有对着真实屏幕才看得出来。主页曾经真的踩中过这个坑（`l1`/`l4` 两处中文标签被设成 `UI_FONT_MID`，后在 `2581830` 改回 `UI_FONT_CN`）；电键页写之前，实现计划里先记下了这条教训，把它同样会用到中文 label 的三处预先标成"必须用 `UI_FONT_CN`"，因此那三处从一开始写的时候就是对的，没有再犯。规则只有一条：**含中文的 label 一律用 `UI_FONT_CN`，绝不能用 `UI_FONT_MID` / `UI_FONT_BIG`**。

## 尚未在真实设备上验证的事项

这台开发机上做不到看屏幕、听声音、摸屏幕、接外部电键、接电池——以下这些只能靠"在设备上过一遍"来确认，**不要假设它们已经没问题**。凡是能靠主机测试、on-device 控件树读取、精确计时、NVS 断电重启回读验证的部分，之前的任务都已经做过（例如"PARIS" 在 20/8 WPM 下实测播放耗时 6813 ms，与按时序公式算出的 6810 ms 只差 3 ms，说明计时链路是可信的），下面这份清单只列**真正没人验证过**的部分，拿到设备后请逐项过一遍：

- [ ] **侧音能不能听见**——完全没人听过，包括是否发声、音调对不对
- [ ] **点划抬起时的渐变消音（防出声爆音）**是否真的听起来是平滑淡出，而不是突兀截断
- [ ] **侧音延迟**手感是否跟得上按键（时序本身已用上面的计时数据验证过，但"手感"是另一回事）
- [ ] **屏幕的实际观感**——排版、字号是否合适，有没有被裁切或重叠的控件（含前面提到的中文字体问题——理论上已改对，但没人在屏幕上确认过）
- [ ] **所有触摸交互**——尤其是电键页的触摸桨处理逻辑：这段代码把 LVGL 触摸事件转换成点/划按键，目前没有任何形式的自动化覆盖，也从未真正被触发执行过（此前"点划正确变声"的联调测试走的是更底层的接口，绕过了触摸事件这一层）
- [ ] **外接电键**——尤其是上面提到的 IO27 划键问题，需要实测确认现象是否如描述
- [ ] **电量百分比准确性**——需要一块已知电量的电池，配合万用表实测电压比对

以下这条不是"没人验证过"，是明确**没有实现**，跟上面清单性质不同，单独记一笔：

- **低电量自动关机（spec 里的 <5% 关机）没有实现。** 设计文档（`docs/superpowers/specs/2026-08-25-cw-trainer-design.md` 第 5 节状态清单）写的是"沿用现有关机"，但 `main/battery.c` 目前只做采样和百分比计算，`ui_home.c` 只在 <15% 时给电量文字加 ⚠，跌破 5%（`battery.c` 里的 `CRITICAL_PERCENT`，与 `fmo-companion/main/battery_monitor.c` 的 `BATTERY_PROTECT_PERCENT` 一致）目前也只打一条 `ESP_LOGW` 告警，没有任何自动关机或降级动作。之所以搁置：这台开发机没有真电池，自动关机这条路径完全没法在真实条件下测过；一个没测过的自动关机、一旦在不该触发的时候误关机，比"完全没有这个功能"更糟；而且这块电路很可能本身已经有硬件级的欠压保护兜底，不依赖软件关机。如果以后要补上，`fmo-companion/main/battery_monitor.c` 里的 `battery_apply_power_state()`/`BATTERY_CRITICAL_PERCENT`/`BATTERY_PROTECT_PERCENT` 一整套状态机可以直接照搬，但补之前请务必先想清楚用什么手段在没有真电池的情况下把关机路径实测一遍（例如临时调低 `EMPTY_MV`/`FULL_MV` 或者改 `to_percent()` 注入假电压），不要写完就当它是好的。
