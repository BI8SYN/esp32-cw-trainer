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

仓库根就是 ESP-IDF 工程本体（`CMakeLists.txt` / `main/` / `partitions.csv`），
`components/` 下是 vendored 的 LVGL、屏/触摸驱动、移植层与字体——**都不是
submodule**，克隆即得全部内容，不依赖任何外部仓库。结构一览见根 `README.md`。

版本号写在 `CMakeLists.txt` 的 `PROJECT_VER`，不取 `git describe`。它被嵌进
bin，设备的设置页和启动日志显示的都是它；CI 在打 tag 时校验 tag 名与它一致
（`.github/workflows/release.yml`），所以两者不会走散。

### 与同一块板子上另一个固件共存

这块板子只有一个 app 分区，**一次只能刷一个固件**。同一块板上常见的另一个
固件是 FMO 伴侣（见 `docs/upstream-source.md`），来回刷是可以的：

- 切换需重刷约 40 秒（460800 波特率下的实测值，见下方"实测收益"）
- **NVS 是共享的**，但两个固件用不同命名空间（本固件是 `cwtrn`），互不污染，
  不必在切换时擦 NVS。正是因为共享，配网页才能一键导入对方存好的 WiFi
  （见 `main/net_store.c`）
- 整机烧录（合并镜像写 `0x0`）会把 NVS 那一段填成 `0xFF`，**两个固件的设置
  一起清空**；想保留就走升级烧录，只写 app，见分发包里的 `README.md`

## 使用

```bash
tools/idf.sh build        # 编译
tools/idf.sh flash        # 烧录（只写 bootloader/分区表/app，不动 NVS）
tools/idf.sh monitor      # 串口监视，Ctrl+] 退出
tools/idf.sh menuconfig   # 配置
tools/idf.sh fullclean    # 清理
```

`tools/idf.sh` 会自动 source `export.sh` 并带上串口参数，不用每次手动进 IDF 环境。
它只做这一件事，之后原样转发给 `idf.py`。

环境变量可覆盖：`IDF_DIR`、`ESP_PORT`、`ESP_BAUD`。

已经在 IDF 环境里（比如 CI 容器）不需要这个脚本，直接 `idf.py build`。

## 波特率：写 460800，读 115200

两者不同是有原因的，**关键在于有没有校验**：

| 操作 | 波特率 | 依据 |
|---|---|---|
| **写入**（`tools/idf.sh flash`） | **460800** | 每段结束做 MD5 校验（日志里的 `Hash of data verified`），传输出错会当场失败并可重试，不会静默写坏 |
| **读取**（`esptool read_flash`） | **115200** | 460800 下实测出现 `Corrupt data`，且**没有任何校验**能发现，会静默读出错误内容。备份必须坚持 115200 |

实测收益：

```
app 分区 2,670,048 字节
115200    117.5 秒    整周期约 4 分钟
460800     30.9 秒    整周期 39.7 秒     ← 快 3.8 倍
```

921600 在握手阶段就失败（`Unable to verify flash chip connection`），属于安全失败——
一个字节都不会写入。

### monitor 例外

`idf.py` 的 `-b` 会同时作用于 `monitor`，而设备控制台固定 115200，
用 460800 打开会是乱码。`tools/idf.sh` 检测到 `monitor` 子命令时会自动降回 115200。

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
| 默认呼号 | `BI8SIG`（上游作者呼号） | NVS 里存的是 `BI8SYN` |
| Audio WS | `ws://192.168.3.165/audio` | `ws://fmo.local/audio` |
| Event WS | `ws://192.168.3.165/events` | `ws://fmo.local/events` |
| Station WS | `ws://192.168.3.165/ws` | `ws://fmo.local/ws` |

这些差异就是版本号里 `-dirty` 的来源——卖家改了默认值再编译。因为 NVS 优先于编译期默认值，重新烧录后设备行为应当不变。

要彻底恢复出厂（含 NVS），用 `tools/restore_flash.sh` 整片写回。
