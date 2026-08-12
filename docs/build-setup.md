# 开发环境搭建

## 结论：macOS 原生支持，不需要 Rosetta / Docker / 虚拟机

| 检查项 | 本机情况 |
|---|---|
| 架构 | Apple Silicon `arm64`，macOS 26.5.2 |
| ESP-IDF 工具清单支持的平台 | 含 `macos-arm64` |
| Xtensa 编译器 `esp-14.2.0_20260121` | 提供 `macos-arm64` 原生构建 |
| Python | 系统 3.9.6（IDF v5.5 要求 ≥3.9）；IDF 另建独立 venv，不污染系统环境 |
| cmake / ninja | 系统里没有，**macOS 上需要显式安装**（见下方"两个安装陷阱"） |
| 串口 | CH340 → `/dev/cu.usbserial-310`，已用 esptool 完整读过两遍 4MB，链路验证过 |

## 版本选择：ESP-IDF v5.5.4

不是随便选的。上游 `dependencies.lock` 里写着：

```yaml
  idf:
    source:
      type: idf
    version: 5.5.4
```

与设备上固件 `esp_app_desc_t` 记录的 `idf_ver = v5.5.4` 完全一致。（上游 README 写的是 v5.5.1，以 lock 文件为准。）

## 安装

```bash
git clone -b v5.5.4 --depth 1 --recursive --shallow-submodules \
    https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32
```

体积：`~/esp/esp-idf` 约 664MB，`~/.espressif`（工具链）约 1.5GB。不想要了直接删这两个目录，无残留。

`install.sh esp32` 只装 ESP32（Xtensa）这一个目标的工具链，不装 ESP32-S3/C3 等，省下不少空间。

### 两个安装陷阱（都踩过了）

#### 陷阱一：macOS 上 install.sh 不装 cmake / ninja

`install.sh` 装完后直接编译会报：

```
"cmake" must be available on the PATH to use idf.py
```

IDF 在 Windows 上自带 cmake/ninja，**macOS / Linux 上默认不装**（tools.json 里标为 `on_request`），假定由系统提供。两种解法：

```bash
# 方案 A（推荐）：让 IDF 装自己那份，全部留在 ~/.espressif，不动系统
~/.espressif/python_env/idf5.5_py3.9_env/bin/python ~/esp/esp-idf/tools/idf_tools.py install cmake ninja

# 方案 B：用 Homebrew
brew install cmake ninja
```

本项目用的是方案 A，装了 cmake 3.30.2 + ninja 1.12.1。好处是卸载时仍然只需删 `~/esp` 和 `~/.espressif` 两个目录。

#### 陷阱二：要等 install.sh 完整跑完

`install.sh` 分两个阶段：先下载解压工具链，**再**用 pip 装 Python 依赖。编译器二进制出现 ≠ 安装完成。

如果在第二阶段结束前就去编译，会看到：

```
error: Unable to import the rich module: No module named 'rich'. Please execute the install script.
ERROR: Activation script failed
```

这不是环境损坏，只是装到一半。等到日志出现 `All done! You can now run: . ./export.sh` 再编译。验证：

```bash
~/.espressif/python_env/idf5.5_py3.9_env/bin/python -c "import rich, esptool, idf_component_manager"
```

顺带一提，安装脚本走的是 `dl.espressif.cn`（国内镜像），下载速度不成问题。

## 项目结构

上游作为 submodule 挂在 `firmware/fmo-companion`：

```bash
git submodule add https://github.com/Johnson-LYS/esp32-fmo-companion.git firmware/fmo-companion
```

指向 **fork**（`Johnson-LYS/esp32-fmo-companion`）而不是上游原仓库，这样自己的改动可以直接推。当前锁定在 `8d372f9`（= 设备上固件对应的 commit，也是上游 `main` 的 HEAD，标签 V1.31）。

克隆后拉取：

```bash
git submodule update --init --recursive
```

## 使用

```bash
tools/idf.sh build        # 编译
tools/idf.sh flash        # 烧录（只写 bootloader/分区表/app，不动 NVS）
tools/idf.sh monitor      # 串口监视，Ctrl+] 退出
tools/idf.sh menuconfig   # 配置
tools/idf.sh fullclean    # 清理
```

`tools/idf.sh` 会自动 source `export.sh` 并带上串口参数，不用每次手动进 IDF 环境。

环境变量可覆盖：`IDF_DIR`、`ESP_PORT`、`ESP_BAUD`、`PROJECT`。

## 波特率：坚持 115200

这块板子的 CH340 在 460800 下读 flash 会出 `Corrupt data` 错误（备份时实测，见 `firmware-dump.md`）。所有脚本默认 115200，写入同理。想快可以试 230400，但没必要——省下的几十秒不值得冒险。

## 关于烧录会不会丢配置

`idf.py flash` 只写三个区域：

| 地址 | 内容 |
|---|---|
| 0x1000 | bootloader |
| 0x8000 | 分区表 |
| 0x10000 | 应用 |

**不碰 NVS（0x9000）**，所以设备已有的 WiFi 配置、设置项、QTH 缓存都会保留。

这一点很重要，因为上游源码里的编译期默认值和设备上的不同：

| 项 | 上游 `app_config.h` | 设备 NVS 实际值 |
|---|---|---|
| 默认呼号 | `BI8SIG`（作者呼号） | `BI4TKL` |
| Audio WS | `ws://192.168.3.165/audio` | `ws://fmo.local/audio` |
| Event WS | `ws://192.168.3.165/events` | `ws://fmo.local/events` |
| Station WS | `ws://192.168.3.165/ws` | `ws://fmo.local/ws` |

这些差异就是版本号里 `-dirty` 的来源——卖家改了默认值再编译。因为 NVS 优先于编译期默认值，重新烧录后设备行为应当不变。

要彻底恢复出厂（含 NVS），用 `tools/restore_flash.sh` 整片写回。
