# 上游源码：原厂固件是开源的

## 结论

设备里的固件**是开源的，MIT 协议，可以自由地基于它开发**。而且能精确定位到源码的具体提交。

| 项目 | 值 |
|---|---|
| 仓库 | https://github.com/zhaozhengde/esp32-fmo-companion |
| 说明 | 基于ESP32的FMO副屏 |
| 协议 | **MIT License**，Copyright © 2026 BI8SIG |
| 对应 commit | **`8d372f9938ef`** — 「V1.3.1,增加wg支持，更换qth查询地址」（2026-07-26） |
| 该 commit 状态 | 仓库 `main` 分支的**最新提交**，卖家刷的就是当时最新版 |
| 上游活跃度 | 2026-05-26 建仓，8 个提交，★10 / fork 6 |

MIT 协议意味着：随便改、随便用、可以闭源分发、可以商用，唯一义务是保留版权声明和许可证文本。

## 怎么确认是同一份代码的

四条独立证据互相印证：

1. **工程名完全一致**。备份镜像 `esp_app_desc_t` 里的 `project = esp32-fmo-companion-WireGuard`，上游 `CMakeLists.txt` 里正是 `project(esp32-fmo-companion-WireGuard)`。
2. **版本号就是 commit 哈希**。app 描述符 `version = 8d372f9-dirty`，该哈希在上游仓库中真实存在。
3. **分区表逐行吻合**。上游 `partitions.csv`：

   ```
   nvs,      data, nvs,     0x9000,   0x6000,
   phy_init, data, phy,     0xf000,   0x1000,
   factory,  app,  factory, 0x10000,  0x300000,
   spiffs,   data, spiffs,  0x310000, 0xF0000,
   ```

   与从设备读出的分区表完全一致（含 spiffs 那 960K）。
4. **源码文件路径吻合**。二进制里残留的 `__FILE__` 字符串（`./main/main.c`、`./main/wifi_manager.c`、`./components/trombik__esp_wireguard/src/wireguardif.c`、`./components/lvgl_esp32_drivers/...`）都能在上游仓库中找到同名文件。

## 关于 `-dirty` 后缀

版本号 `8d372f9-dirty` 中的 `-dirty` 表示**编译时工作区有未提交的改动**。所以卖家刷的固件与 `8d372f9` 可能有细微差异——常见的是改了 `sdkconfig`（WiFi 凭据、默认呼号、服务器地址之类），也可能改了代码。

从 NVS 里能看到默认呼号被设成了 `BI4TKL`，WebSocket 默认地址是 `ws://fmo.local/...`，这些很可能就是那些本地改动。

**影响**：基于上游 `8d372f9` 重新编译，得到的固件与设备里的可能不是 100% 相同，但功能基线一致。不必追求二进制级复现。

## 项目背景

来自上游 README：

- **FMO 主机由 BG5ESN 开发**，是一套面向业余无线电爱好者的**互联网对讲硬件系统**。
- 本项目（FMO 伴侣）由 **BI8SIG** 开发，目的是用便宜的 ESP32 开发板实现 FMO 的**仪表盘/副屏**功能。
- 开发环境：**ESP-IDF v5.5.1**（设备上这份实际用 v5.5.4 编译），UI 基于 **LVGL**，屏幕 ST7789，触摸 XPT2046，音频走 ESP32 内置 DAC。
- 蓝牙音频部分代码写了，但受限于 ESP32 性能**没能跑起来**（`bt_audio.c` 存在但未启用）。
- 上游 README 的 GPIO 表与本机硬件**完全一致**，说明这个项目就是针对这块板子（CYD ESP32-2432S028R）开发的。

相关生态：

| 项目 | 说明 |
|---|---|
| [fmo.bg5eit.cn](http://fmo.bg5eit.cn/) | FMO 导航站（FMOC 控制台，桌面版/手机版） |
| [niufox/fmo-mobile-controller](https://github.com/niufox/fmo-mobile-controller) | FMO 手机端控制器（JavaScript，无 license） |
| [54dashayu/fmo-companion-m5core](https://github.com/54dashayu/fmo-companion-m5core) | 同类固件的 M5Stack Core 移植版 |
| [hansgao0422/ESP32-FMO-Mate---LilyGO-T-LoRa-Pager](https://github.com/hansgao0422/ESP32-FMO-Mate---LilyGO-T-LoRa-Pager) | LilyGO T-LoRa-Pager 版 FMO 伴侣 |
| [Johnson-LYS/FMO-Companion](https://github.com/Johnson-LYS/FMO-Companion) | iOS 版 FMO Companion |

## 上游代码规模

排除内置的 LVGL（1195 个文件）和 lvgl_esp32_drivers（82 个文件）后，应用自身代码：

| 文件 | 大小 | 职责 |
|---|---|---|
| `main/app_ui.c` | 207 KB | **UI 全部逻辑（单文件，偏大）** |
| `main/audio_ws.c` | 102 KB | 音频 WebSocket + 缓冲策略 |
| `main/app_settings.c` | 63 KB | 设置项 + NVS 持久化 + 版本迁移 |
| `main/wifi_manager.c` | 40 KB | 多 WiFi 配置档、按 RSSI 自动选择 |
| `main/gpio_ctrl.c` | 25 KB | GPIO/背光/RGB/蜂鸣器 |
| `main/bt_audio.c` | 23 KB | 蓝牙音频（未启用） |
| `main/wg_web_config.c` | 21 KB | WireGuard 的 HTTP 配置页 |
| `main/qth_lookup.c` | 18 KB | 呼号→网格查询 + NVS 缓存 |
| `main/event_parser.c` | 16 KB | 事件 WebSocket 解析 |
| `main/ui_async.c` | 16 KB | UI 异步任务 |
| `main/station_parser.c` | 13 KB | 电台列表解析 |
| `main/main.c` | 12 KB | 启动流程 |
| `main/battery_monitor.c` | 19 KB | 电池 ADC + 校准 |
| `main/audio_output.c` | 10 KB | I2S 内置 DAC 输出 |
| `main/wg_vpn.c` | 6.9 KB | WireGuard 生命周期 |
| `main/app_power_save.c` | 5.9 KB | 省电 |
| `main/fonts/*.c` | 约 57 MB 源码 | 字体点阵（SimHei 16/20/24/32 等） |
| `main/ui_icons.c` | 191 KB | 图标位图 |

`app_ui.c` 单文件 207KB 是这个项目最明显的结构问题——如果要在它基础上做大改动，第一件事大概率是拆分它。

## 依赖的第三方组件及协议

| 组件 | 协议 | 说明 |
|---|---|---|
| ESP-IDF v5.5.x | Apache-2.0 | 乐鑫官方 SDK |
| LVGL（内置在 components/） | MIT | GUI 库 |
| lvgl_esp32_drivers | MIT | ST7789 + XPT2046 的 SPI 驱动层 |
| trombik/esp_wireguard | BSD-3-Clause | WireGuard for ESP-IDF |

全都是宽松协议，没有 GPL 传染问题。

## 可选的开发路径

| 路径 | 说明 |
|---|---|
| **Fork 上游改** | 直接基于 `8d372f9` 开发，保留全部现有功能，增量修改 |
| **重写，参考上游** | 从空白 ESP-IDF 工程开始，需要时抄上游的驱动/协议实现 |
| **只抄硬件适配层** | 拿 `board_config.h` + `lv_porting` 做硬件基础，业务全新写 |

选哪条取决于你想做什么——这个等你说完需求再定。

## 复现查证

```bash
# 确认 commit 存在
curl -s https://api.github.com/repos/zhaozhengde/esp32-fmo-companion/commits/8d372f9 | grep '"message"'

# 拉源码
git clone https://github.com/zhaozhengde/esp32-fmo-companion.git
cd esp32-fmo-companion && git checkout 8d372f9
```
