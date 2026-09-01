#!/usr/bin/env bash
# 把本机编好的固件烧到另一台机器上插着的板子。
#
# 为什么不直接在那台机器上 idf.sh flash：那边的仓库停在一个很旧的提交
# （实测 278c093，和本机 HEAD 差着一大截），在那边编等于编了个别的东西。
# 所以分工是——**本机编译，远端只负责把字节推进串口**。远端需要的只有
# esptool，而 ESP-IDF 自带的 python 环境里就有，不需要它的仓库是新的。
#
# 用法：
#   tools/flash_remote.sh                    # 烧 firmware/cw-trainer 的当前构建
#   PROJECT=firmware/fmo-companion tools/flash_remote.sh
#   tools/flash_remote.sh --monitor          # 烧完接着看 20 秒串口输出
#
# 环境变量：
#   REMOTE       默认 liyongsheng@10.11.26.88
#   REMOTE_PORT  默认 /dev/cu.usbserial-2110（那台机器上的实测值，换线会变）
#   REMOTE_PY    远端 esptool 所在的 python，默认自动找 ~/.espressif 下的 IDF 环境
#   ESP_BAUD     默认 460800。写入每段都有 MD5 校验，出错会当场失败、不会静默写坏
set -euo pipefail

cd "$(dirname "$0")/.."

PROJECT="${PROJECT:-firmware/cw-trainer}"
REMOTE="${REMOTE:-liyongsheng@10.11.26.88}"
PORT="${REMOTE_PORT:-/dev/cu.usbserial-2110}"
BAUD="${ESP_BAUD:-460800}"
BUILD="$PROJECT/build"
MONITOR=0
[[ "${1:-}" == "--monitor" ]] && MONITOR=1

# 构建产物齐不齐先在本机查，别等 scp 到一半才发现少文件
FILES=(
  "bootloader/bootloader.bin"
  "partition_table/partition-table.bin"
  "ota_data_initial.bin"
  "$(basename "$PROJECT").bin"
)
for f in "${FILES[@]}"; do
  [[ -f "$BUILD/$f" ]] || { echo "错误：缺少 $BUILD/$f，先跑一次 tools/idf.sh build" >&2; exit 1; }
done

APP="$(basename "$PROJECT").bin"
echo "== 本机构建 =="
echo "   $APP  $(wc -c < "$BUILD/$APP") 字节"

# 远端找 esptool。IDF 的 python 环境目录名带版本号，写死会在升级后失效
REMOTE_PY="${REMOTE_PY:-}"
if [[ -z "$REMOTE_PY" ]]; then
  REMOTE_PY=$(ssh -o BatchMode=yes "$REMOTE" \
    'ls -d ~/.espressif/python_env/*/bin/python 2>/dev/null | head -1')
  [[ -n "$REMOTE_PY" ]] || { echo "错误：远端找不到 ESP-IDF 的 python 环境（esptool 在那里面）" >&2; exit 1; }
fi

TMP="/tmp/cw-flash-$$"
echo "== 传输到 $REMOTE:$TMP =="
ssh -o BatchMode=yes "$REMOTE" "mkdir -p $TMP/bootloader $TMP/partition_table"
for f in "${FILES[@]}"; do
  scp -q "$BUILD/$f" "$REMOTE:$TMP/$f"
done

echo "== 烧录 $PORT @ $BAUD =="
# shellcheck disable=SC2029
ssh -o BatchMode=yes "$REMOTE" "cd $TMP && '$REMOTE_PY' -m esptool \
  --chip esp32 -b $BAUD -p $PORT \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m \
  0x1000 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 ota_data_initial.bin \
  0x20000 $APP"

if [[ $MONITOR == 1 ]]; then
  echo "== 串口输出（20 秒，控制台固定 115200）=="
  # miniterm 要 TTY，这里只要单向读，用 pyserial 直接读更省事
  ssh -o BatchMode=yes "$REMOTE" "'$REMOTE_PY' - <<'PY'
import serial, time
s = serial.Serial('$PORT', 115200, timeout=1)
end = time.time() + 20
while time.time() < end:
    d = s.read(4096)
    if d:
        print(d.decode('utf-8', 'replace'), end='', flush=True)
PY"
fi

ssh -o BatchMode=yes "$REMOTE" "rm -rf $TMP"
echo "== 完成 =="
