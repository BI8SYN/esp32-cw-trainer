#!/usr/bin/env bash
# 在 ESP-IDF 环境中对本工程执行 idf.py。
#
# 用法： tools/idf.sh <idf.py 子命令> [参数...]
#   例： tools/idf.sh build
#        tools/idf.sh flash
#        tools/idf.sh monitor
#        tools/idf.sh menuconfig
#
# 环境变量：
#   IDF_DIR    ESP-IDF 安装位置，默认 ~/esp/esp-idf
#   ESP_PORT   串口，默认 /dev/cu.usbserial-310
#   ESP_BAUD   波特率，默认 460800（实测见下方说明）
#
# 已经 source 过 export.sh 的环境（比如 CI 容器）直接 idf.py build 即可，
# 不需要这个脚本。
set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"
PORT="${ESP_PORT:-/dev/cu.usbserial-310}"
# 写入用 460800：实测 app 分区烧录 117.5s -> 30.9s，三处 MD5 校验全部通过。
#
# 读取（read_flash）则必须坚持 115200：460800 下实测会出现 Corrupt data，
# 且**没有校验**能发现，会静默读出错误内容。写入每段结束都做 MD5 校验
# （日志里的 Hash of data verified），传输出错会当场失败并可重试。
#
# 921600 实测在握手阶段就失败（Unable to verify flash chip connection），
# 属于安全失败——一个字节都不会写入。
BAUD="${ESP_BAUD:-460800}"

[[ -f "$IDF_DIR/export.sh" ]] || {
  echo "错误：找不到 ESP-IDF（$IDF_DIR/export.sh）" >&2
  echo "安装： git clone -b v5.5.4 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf" >&2
  echo "      ~/esp/esp-idf/install.sh esp32" >&2
  exit 1
}

# export.sh 里有未定义变量引用，先关掉 -u
set +u
# shellcheck disable=SC1091
source "$IDF_DIR/export.sh" > /dev/null
set -u

cd "$REPO_ROOT"

# idf.py 的 -b 会同时作用于 monitor，而设备控制台固定 115200，
# 用 460800 打开会是乱码。因此 monitor 单独降回 115200。
for arg in "$@"; do
    if [[ "$arg" == "monitor" ]]; then
        BAUD=115200
        break
    fi
done

exec idf.py -p "$PORT" -b "$BAUD" "$@"
