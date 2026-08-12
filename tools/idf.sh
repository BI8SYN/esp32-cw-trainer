#!/usr/bin/env bash
# 在 ESP-IDF 环境中对 firmware/fmo-companion 执行 idf.py。
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
#   ESP_BAUD   波特率，默认 115200（这块板子的 CH340 在高波特率下不可靠）
#   PROJECT    工程目录，默认 firmware/fmo-companion
set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"
PORT="${ESP_PORT:-/dev/cu.usbserial-310}"
BAUD="${ESP_BAUD:-115200}"
PROJECT="${PROJECT:-firmware/fmo-companion}"

[[ -f "$IDF_DIR/export.sh" ]] || {
  echo "错误：找不到 ESP-IDF（$IDF_DIR/export.sh）" >&2
  echo "安装： git clone -b v5.5.4 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf" >&2
  echo "      ~/esp/esp-idf/install.sh esp32" >&2
  exit 1
}

[[ -f "$REPO_ROOT/$PROJECT/CMakeLists.txt" ]] || {
  echo "错误：$PROJECT 里没有 CMakeLists.txt。submodule 拉下来了吗？" >&2
  echo "      git submodule update --init --recursive" >&2
  exit 1
}

# export.sh 里有未定义变量引用，先关掉 -u
set +u
# shellcheck disable=SC1091
source "$IDF_DIR/export.sh" > /dev/null
set -u

cd "$REPO_ROOT/$PROJECT"
exec idf.py -p "$PORT" -b "$BAUD" "$@"
