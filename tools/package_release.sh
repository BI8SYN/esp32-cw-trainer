#!/usr/bin/env bash
# 打一个可直接发给别人的固件包，输出到 dist/cw-trainer-<版本>/ 并压成同名 .zip。
# 目录结构、文件命名、README 章节都沿用 v1.0.0 那一版手工打的包的约定。
#
# 用法： tools/package_release.sh
#
# 环境变量：
#   DIST_DIR   输出的父目录，默认仓库的 dist/（不进 git，见 .gitignore）
#   IDF_DIR    本机 ESP-IDF 位置，默认 ~/esp/esp-idf；已在 PATH 上有 idf.py
#              时（CI 容器）忽略此项，不再 source export.sh
#
# 版本号取 CMakeLists.txt 里的 PROJECT_VER——那才是真正嵌进 bin、设备设置页和
# 启动日志显示的那个值。CI 在打 tag 时另会校验 tag 名与它一致。
set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

NAME="cw-trainer"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"

VER="$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' CMakeLists.txt)"
[[ -n "$VER" ]] || {
  echo "错误：CMakeLists.txt 里没读到 PROJECT_VER" >&2
  exit 1
}

BUILD="$REPO_ROOT/build"
APP_BIN="$BUILD/$NAME.bin"
[[ -f "$APP_BIN" ]] || {
  echo "错误：找不到 ${APP_BIN}，先构建：tools/idf.sh build" >&2
  exit 1
}

# 构建产物必须比所有源文件新，否则打出来的包和当前代码对不上，而且这种错配
# 不会有任何报错——烧进去才发现是旧固件。
NEWER="$(find main components -type f -newer "$APP_BIN" -print -quit 2>/dev/null || true)"
[[ -z "$NEWER" ]] || {
  echo "错误：$NEWER 比构建产物新，先重新构建：tools/idf.sh build" >&2
  exit 1
}

# 嵌进 bin 里的版本号要和 PROJECT_VER 对得上，防止改了 CMakeLists 却没重新构建。
#
# 用进程替换而不是 `strings ... | grep -qx`：grep -q 匹配到第一条就退出，
# strings 随即吃到 SIGPIPE 非零退出，脚本开头的 set -o pipefail 会把整条管道
# 判成失败——版本明明对得上，检查却报错。
#
# strings 属于 binutils，精简的 CI 容器里可能没有，退到直接在二进制里找
# ——代价是变成子串匹配（v1.5.1 也会匹配 v1.5.10），够用。
if command -v strings > /dev/null; then
  VER_OK=$(grep -qx "$VER" <(strings "$APP_BIN") && echo yes || echo no)
else
  VER_OK=$(LC_ALL=C grep -qaF "$VER" "$APP_BIN" && echo yes || echo no)
fi
[[ "$VER_OK" == yes ]] || {
  echo "错误：$APP_BIN 里找不到版本字符串 ${VER}，构建产物与 CMakeLists 不同步" >&2
  exit 1
}

COMMIT="$(git rev-parse --short HEAD)"

# ── 环境：CI 容器里 idf.py 已在 PATH 上，本机则需要 source 一次 export.sh
if ! command -v idf.py > /dev/null; then
  IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"
  [[ -f "$IDF_DIR/export.sh" ]] || {
    echo "错误：idf.py 不在 PATH 上，也找不到 $IDF_DIR/export.sh" >&2
    exit 1
  }
  set +u
  # shellcheck disable=SC1091
  source "$IDF_DIR/export.sh" > /dev/null
  set -u
fi

# IDF 版本写进包里的 README。容器里有 IDF_VERSION 环境变量，本机则从 IDF 的
# git 仓库问；两条都没有就留空，而不是硬编一个可能已经过期的版本号。
IDF_VER="${IDF_VERSION:-}"
if [[ -z "$IDF_VER" ]]; then
  IDF_VER="$(git -C "${IDF_PATH:-${IDF_DIR:-$HOME/esp/esp-idf}}" describe --tags 2>/dev/null || echo "未知")"
fi

# sha256：macOS 只有 shasum，多数 Linux 只有 sha256sum
if command -v sha256sum > /dev/null; then
  SHA() { sha256sum "$@"; }
else
  SHA() { shasum -a 256 "$@"; }
fi

OUT="$DIST_DIR/$NAME-$VER"
rm -rf "$OUT"
mkdir -p "$OUT"

echo "==> 合并镜像"
idf.py merge-bin -o "$OUT/$NAME-$VER-合并版.bin" > /dev/null

echo "==> 收集分段镜像"
cp "$BUILD/bootloader/bootloader.bin"           "$OUT/bootloader.bin"
cp "$BUILD/partition_table/partition-table.bin" "$OUT/partition-table.bin"
cp "$APP_BIN"                                    "$OUT/$NAME.bin"

echo "==> 生成 README（正文见 dist-readme.md）"
sed -e "s/@VER@/$VER/g" -e "s/@COMMIT@/$COMMIT/g" -e "s/@IDF@/$IDF_VER/g" \
    dist-readme.md > "$OUT/README.md"

echo "==> 校验和"
(cd "$OUT" && SHA ./*.bin | sed 's|\./||' > SHA256SUMS.txt)

echo "==> 打包"
# zip 在 CI 容器里未必装了；python 自带的 zipfile 能顶上，不为这个多装一个包。
ZIP="$DIST_DIR/$NAME-$VER.zip"
rm -f "$ZIP"
if command -v zip > /dev/null; then
  (cd "$DIST_DIR" && zip -qr "$(basename "$ZIP")" "$NAME-$VER")
else
  (cd "$DIST_DIR" && python3 -c '
import sys, zipfile, pathlib
zip_name, folder = sys.argv[1], pathlib.Path(sys.argv[2])
with zipfile.ZipFile(zip_name, "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(folder.rglob("*")):
        z.write(f, f.relative_to(folder.parent))
' "$(basename "$ZIP")" "$NAME-$VER")
fi

echo
echo "完成："
echo "  目录：  $OUT/"
echo "  压缩包：$ZIP"
ls -lh "$OUT"
