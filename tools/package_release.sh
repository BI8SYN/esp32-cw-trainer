#!/usr/bin/env bash
# 打一个可直接发给别人的固件包，输出到 ~/Desktop/<工程>-<版本>/ 并压成同名 .zip。
# 目录结构、文件命名、README 章节都沿用 v1.0.0 那一版手工打的包的约定。
#
# 用法： tools/package_release.sh [工程目录]
#   例： tools/package_release.sh firmware/cw-trainer
#
# 环境变量：
#   DIST_DIR   输出的父目录，默认 ~/Desktop
#
# 版本号取工程 CMakeLists.txt 里的 PROJECT_VER，不是仓库 tag——两个固件共用
# tag 命名空间，只有 PROJECT_VER 才是这个固件真正嵌进 bin 里的那个值
# （设备设置页和启动日志显示的也是它）。
set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

PROJECT="${1:-firmware/cw-trainer}"
NAME="$(basename "$PROJECT")"
DIST_DIR="${DIST_DIR:-$HOME/Desktop}"

[[ -f "$REPO_ROOT/$PROJECT/CMakeLists.txt" ]] || {
  echo "错误：$PROJECT 里没有 CMakeLists.txt" >&2
  exit 1
}

VER="$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' "$REPO_ROOT/$PROJECT/CMakeLists.txt")"
[[ -n "$VER" ]] || {
  echo "错误：$PROJECT/CMakeLists.txt 里没读到 PROJECT_VER" >&2
  exit 1
}

BUILD="$REPO_ROOT/$PROJECT/build"
APP_BIN="$BUILD/$NAME.bin"
[[ -f "$APP_BIN" ]] || {
  echo "错误：找不到 ${APP_BIN}，先构建：PROJECT=$PROJECT tools/idf.sh build" >&2
  exit 1
}

# 构建产物必须比所有源文件新，否则打出来的包和当前代码对不上，而且这种错配
# 不会有任何报错——烧进去才发现是旧固件。
NEWER="$(find "$REPO_ROOT/$PROJECT/main" "$REPO_ROOT/components" "$REPO_ROOT/fonts" \
           -type f -newer "$APP_BIN" -print -quit 2>/dev/null || true)"
[[ -z "$NEWER" ]] || {
  echo "错误：$NEWER 比构建产物新，先重新构建：" >&2
  echo "      PROJECT=$PROJECT tools/idf.sh build" >&2
  exit 1
}

# 嵌进 bin 里的版本号要和 PROJECT_VER 对得上，防止改了 CMakeLists 却没重新构建。
#
# 用进程替换而不是 `strings ... | grep -qx`：grep -q 匹配到第一条就退出，
# strings 随即吃到 SIGPIPE 非零退出，脚本开头的 set -o pipefail 会把整条管道
# 判成失败——版本明明对得上，检查却报错。
grep -qx "$VER" <(strings "$APP_BIN") || {
  echo "错误：$APP_BIN 里找不到版本字符串 ${VER}，构建产物与 CMakeLists 不同步" >&2
  exit 1
}

COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
IDF_VER="$(git -C "${IDF_DIR:-$HOME/esp/esp-idf}" describe --tags 2>/dev/null || echo "v5.5.4")"

OUT="$DIST_DIR/$NAME-$VER"
rm -rf "$OUT"
mkdir -p "$OUT"

echo "==> 合并镜像"
IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"
set +u
# shellcheck disable=SC1091
source "$IDF_DIR/export.sh" > /dev/null
set -u
(cd "$REPO_ROOT/$PROJECT" && idf.py merge-bin -o "$OUT/$NAME-$VER-合并版.bin" > /dev/null)

echo "==> 收集分段镜像"
cp "$BUILD/bootloader/bootloader.bin"           "$OUT/bootloader.bin"
cp "$BUILD/partition_table/partition-table.bin" "$OUT/partition-table.bin"
cp "$APP_BIN"                                    "$OUT/$NAME.bin"

echo "==> 生成 README（正文见 $PROJECT/dist-readme.md）"
sed -e "s/@VER@/$VER/g" -e "s/@COMMIT@/$COMMIT/g" -e "s/@IDF@/$IDF_VER/g" \
    "$REPO_ROOT/$PROJECT/dist-readme.md" > "$OUT/README.md"

echo "==> 校验和"
(cd "$OUT" && shasum -a 256 ./*.bin | sed 's|\./||' > SHA256SUMS.txt)

echo "==> 打包"
ZIP="$DIST_DIR/$NAME-$VER.zip"
rm -f "$ZIP"
(cd "$DIST_DIR" && zip -qr "$(basename "$ZIP")" "$NAME-$VER")

echo
echo "完成："
echo "  目录：  $OUT/"
echo "  压缩包：$ZIP"
ls -lh "$OUT"
