# 第三方组件与素材出处

本仓库以 MIT 分发（见 `LICENSE`）。以下是随仓库一起分发的第三方代码与素材，
逐项列明来源与许可。

## 代码

| 组件 | 来源 | 许可 |
|---|---|---|
| `components/lvgl/` | [lvgl/lvgl](https://github.com/lvgl/lvgl) v8.3.11 | MIT（`components/lvgl/LICENCE.txt`） |
| `components/lvgl_esp32_drivers/` | [lvgl/lvgl_esp32_drivers](https://github.com/lvgl/lvgl_esp32_drivers)（ST7789 显示 + XPT2046 触摸） | MIT（`components/lvgl_esp32_drivers/LICENSE`） |
| `components/lv_porting/` | 上游 [zhaozhengde/esp32-fmo-companion](https://github.com/zhaozhengde/esp32-fmo-companion) commit `8d372f9` 的显示/输入移植层 | MIT |

### 对 vendored LVGL 的改动

`components/lvgl/env_support/cmake/esp.cmake`：原始 glob 无条件把 `demos/*.c`
编进来。本仓库不带 LVGL 的 `demos/` `examples/` `tests/` `docs/` `scripts/`
——它们只在官方示例里用得到，却占了上游仓库 99MB 中的 80MB。相应地删掉了
按 `CONFIG_LV_USE_DEMO_*` 追加 demo 源码的分支和 examples 的 include 目录。
LVGL 库代码本身（`src/`）一个字节没改。

## 本项目的来源

设备原厂固件是开源的，本项目在它之上起步：上游
[zhaozhengde/esp32-fmo-companion](https://github.com/zhaozhengde/esp32-fmo-companion)
commit `8d372f9`，MIT，Copyright © 2026 BI8SIG。溯源过程（四条独立证据）见
`docs/upstream-source.md`。

CW 练习机的固件代码（`main/`）、主机测试（`test/`）、工具链（`tools/`）与
全部文档为本项目原创。

## 字体

字体点阵以 C 源码形式编进固件（LVGL 的字体格式），共四个：

| 文件 | 来源 | 可否重新生成 |
|---|---|---|
| `components/fonts/font28cn.c` | [思源宋体 SC](https://github.com/notofonts/noto-cjk)（Noto Serif SC），**SIL Open Font License 1.1** | ✅ `tools/gen_title_font.sh`，脚本里写着字体 URL 和 `lv_font_conv` 命令 |
| `components/fonts/font20_cwtrn.c` | 由 `tools/subset_font.py` 从 `tools/font-src/font20.c` 裁剪，字形数据逐像素一致 | ✅ 但底字体同下 |
| `tools/font-src/font20.c`（不参与编译，只作裁剪原料） | 上游 esp32-fmo-companion，由「Lvgl Font Tool V0.4」生成 | ❌ |
| `components/fonts/font28.c` | 同上 | ❌ |
| `components/fonts/EN_BIG.c` | 同上 | ❌ |

**底层字体来源不明这件事要说清楚。** 后三个文件的文件头只记着「本字体文件由
Lvgl Font Tool V0.4 生成」，没有记录输入的是哪个字体。它们随上游仓库的 MIT
授权一并继承而来，本仓库照 MIT 继续分发；但如果你要把这个固件用在对字体授权
有严格要求的场合，**请自行核实或替换**。

替换路径是清楚的、也已经走通过一次：`font28cn.c` 就是用
`tools/gen_title_font.sh` 从 OFL 字体重新生成的。把正文字体也照这个办法用思源黑
重做一遍，是本项目记在案的待办（见 `docs/optimization-backlog.md`）。

## 硬件文档

`docs/hardware.md` 的引脚与外设信息整理自厂商随机附带的开发文档与实测。
**厂商文档本身（PDF）不随本仓库分发。**
