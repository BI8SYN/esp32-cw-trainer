#!/usr/bin/env bash
# 把本机编好的固件烧到另一台机器上插着的板子。
#
# 为什么不直接在那台机器上 idf.sh flash：那边的仓库停在一个很旧的提交
# （实测 278c093，和本机 HEAD 差着一大截），在那边编等于编了个别的东西。
# 所以分工是——**本机编译，远端只负责把字节推进串口**。远端需要的只有
# esptool，而 ESP-IDF 自带的 python 环境里就有，不需要它的仓库是新的。
#
# 用法：
#   REMOTE=用户@主机 tools/flash_remote.sh              # 烧当前构建
#   REMOTE=用户@主机 tools/flash_remote.sh --monitor    # 烧完接着看 20 秒串口输出
#
# 环境变量：
#   REMOTE       远端 ssh 目标，**必填**（用户@主机）
#   REMOTE_PORT  串口。**默认自动探测**，不写死——USB 重新枚举一次名字就会变
#                （实测 -2110 变成了 -2120），而这正是本仓库 README 反复警告
#                的那个坑。探测到 0 个或多于 1 个才需要手动指定。
#   REMOTE_PY    远端 esptool 所在的 python，默认自动找 ~/.espressif 下的 IDF 环境
#   ESP_BAUD     默认 460800。写入每段都有 MD5 校验，出错会当场失败、不会静默写坏
set -euo pipefail

cd "$(dirname "$0")/.."

REMOTE="${REMOTE:-}"
[[ -n "$REMOTE" ]] || { echo "错误：请设置 REMOTE=用户@主机" >&2; exit 1; }
PORT="${REMOTE_PORT:-}"
BAUD="${ESP_BAUD:-460800}"
BUILD="build"
APP_NAME="cw-trainer"
MONITOR=0
[[ "${1:-}" == "--monitor" ]] && MONITOR=1

# 构建产物齐不齐先在本机查，别等 scp 到一半才发现少文件
FILES=(
  "bootloader/bootloader.bin"
  "partition_table/partition-table.bin"
  "ota_data_initial.bin"
  "$APP_NAME.bin"
)
for f in "${FILES[@]}"; do
  [[ -f "$BUILD/$f" ]] || { echo "错误：缺少 $BUILD/$f，先跑一次 tools/idf.sh build" >&2; exit 1; }
done

APP="$APP_NAME.bin"
echo "== 本机构建 =="
echo "   $APP  $(wc -c < "$BUILD/$APP") 字节"

# 远端找 esptool。IDF 的 python 环境目录名带版本号，写死会在升级后失效
REMOTE_PY="${REMOTE_PY:-}"
if [[ -z "$REMOTE_PY" ]]; then
  REMOTE_PY=$(ssh -o BatchMode=yes "$REMOTE" \
    'ls -d ~/.espressif/python_env/*/bin/python 2>/dev/null | head -1')
  [[ -n "$REMOTE_PY" ]] || { echo "错误：远端找不到 ESP-IDF 的 python 环境（esptool 在那里面）" >&2; exit 1; }
fi

if [[ -z "$PORT" ]]; then
  # 不用通配符：远端登录 shell 是 zsh，通配符没匹配到会中止整条命令而不是
  # 静默返回空。列目录再 grep 才是与 shell 无关的写法。
  # 不用 mapfile：macOS 自带的是 bash 3.2，没有这个内建。
  ports=()
  while IFS= read -r line; do
    [[ -n "$line" ]] && ports+=("/dev/$line")
  done < <(ssh -o BatchMode=yes "$REMOTE" \
    "ls /dev | grep -E '^cu\\.(usbserial|wchusbserial|SLAB_USBtoUART)'" || true)
  case ${#ports[@]} in
    0) echo "错误：远端没有 USB 串口。板子插着吗？" >&2; exit 1 ;;
    1) PORT="${ports[0]}"; echo "   自动探测到串口 $PORT" ;;
    *) echo "错误：远端有多个 USB 串口，用 REMOTE_PORT 指定其中之一：" >&2
       printf '   %s\n' "${ports[@]}" >&2; exit 1 ;;
  esac
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
  echo "== 串口输出（${MON_SECS:-20} 秒，控制台固定 115200）=="
  # **先落盘再回传，不要边读边打印。**
  #
  # 原来是远端 python 一边读串口一边 print，输出经 ssh 管道实时流回来。管道一有
  # 背压，读串口的循环就被拖慢，OS 的串口缓冲随即溢出丢字节——导图时表现为
  # 整段像素错位（实测 10739 行里坏 7 行，而且专挑要量的地方坏）。
  #
  # 落盘之后读循环只和串口打交道，不受网络影响；传输放到采集结束之后做。
  ssh -o BatchMode=yes "$REMOTE" "'$REMOTE_PY' - <<'PY' > $TMP/serial.log
import serial, time
s = serial.Serial('$PORT', 115200, timeout=1)
end = time.time() + ${MON_SECS:-20}
buf = []
while time.time() < end:
    d = s.read(65536)
    if d:
        buf.append(d)
print(b''.join(buf).decode('utf-8', 'replace'), end='')
PY"
  ssh -o BatchMode=yes "$REMOTE" "cat $TMP/serial.log"
fi

ssh -o BatchMode=yes "$REMOTE" "rm -rf $TMP"
echo "== 完成 =="
