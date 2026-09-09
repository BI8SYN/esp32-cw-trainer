#!/usr/bin/env bash
# 把当前构建产物发布为 OTA 包：上传带版本号的固件，并更新版本清单。
#
# 用法： tools/publish_ota.sh
#
# 这一步**不在 CI 里**：让 GitHub Actions 发 OTA 需要把服务器 SSH 私钥放进
# 公开仓库的 secrets，为省一条命令不值得。tag 推上去后 CI 负责 GitHub Release，
# OTA 由维护者在本机手动发。
#
# 环境变量：
#   OTA_HOST   服务器 ssh 别名，默认 ali99
#   OTA_DIR    服务器上的静态目录，默认 /opt/morsekob/www
#   OTA_BASE   设备访问的基地址，默认 http://8.138.130.141:7891
#
# 版本号取 CMakeLists.txt 里的 PROJECT_VER——那才是真正嵌进 bin、设备设置页和
# 启动日志显示的那个值。
set -euo pipefail

cd "$(dirname "$0")/.."

NAME="cw-trainer"
HOST="${OTA_HOST:-ali99}"
DIR="${OTA_DIR:-/opt/morsekob/www}"
BASE="${OTA_BASE:-http://8.138.130.141:7891}"

VER="$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' CMakeLists.txt)"
if [[ -z "$VER" ]]; then
  echo "错误：在 CMakeLists.txt 里找不到 PROJECT_VER" >&2
  exit 1
fi

BIN="build/$NAME.bin"
if [[ ! -f "$BIN" ]]; then
  echo "错误：$BIN 不存在，先 build" >&2
  exit 1
fi

# 版本说明是发布的一部分，缺了就不让发——设备上看不到它（字库放不下），
# 但仓库里必须留下这一版改了什么。
NOTES="docs/releases/$NAME-$VER.md"
if [[ ! -f "$NOTES" ]]; then
  echo "错误：缺少版本说明 $NOTES" >&2
  echo "每个发布版本都要有说明。新建该文件后重试。" >&2
  exit 1
fi

echo "发布 $NAME $VER"
scp -q "$BIN" "$HOST:/tmp/$NAME-$VER.bin"
ssh "$HOST" "install -m 644 /tmp/$NAME-$VER.bin $DIR/$NAME-$VER.bin && rm -f /tmp/$NAME-$VER.bin"

# 清单指向带版本号的那个文件。设备先读清单拿到版本号做比对，
# 再按里面的 url 去下载。
ssh "$HOST" "printf '{\"version\":\"$VER\",\"url\":\"$BASE/$NAME-$VER.bin\"}\n' > $DIR/$NAME.json"

echo "校验："
curl -s --max-time 20 "$BASE/$NAME.json"
curl -s -o /dev/null -w "  固件 HTTP %{http_code}  %{size_download} bytes\n" \
     --max-time 120 "$BASE/$NAME-$VER.bin"
echo "完成。设备端：设置 → 常规 → 固件升级"
