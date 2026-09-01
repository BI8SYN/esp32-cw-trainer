#!/usr/bin/env bash
# 重新生成标题字体 fonts/font28cn.c。
#
# ── 为什么会有这个脚本 ──
#
# fonts/ 下原有的 font20.c / font28.c / EN_BIG.c 都是第三方「Lvgl Font Tool
# V0.4」生成的 XBF 格式，**仓库里没有它们的源字体**——这正是 tools/subset_font.py
# 只能做 C→C 裁剪的原因（见那个文件的头注释）。这是个历史包袱：想换字号、
# 换字重、补字形，都无从下手。
#
# 新加的字体不再重蹈覆辙：字体来源、字号、字符集、生成命令全部写在这里，
# 任何时候都能一字不差地重跑出同一份 .c。
#
# ── 为什么是宋体，为什么只有标题用 ──
#
# 视觉方案（docs/design/cw-trainer-visual-direction.html）定的是「标题宋体 +
# 正文黑体」：28px 撑得住宋体的横细竖粗，最贴电报房调性；20px 正文留黑体保证
# 小字可读。所以这里只生成 28px，**不碰 font20_cwtrn**——换正文字体会让全部
# 中文渲染都变，风险大且方案本来就不要求。
#
# 字体选思源宋（Noto Serif SC），OFL 许可，可以嵌进固件分发。系统自带的苹方、
# 华文黑体是 Apple 授权的，不能用。
#
# 依赖：node + npx（会临时装 lv_font_conv）、curl
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"

FONT_URL="https://github.com/notofonts/noto-cjk/raw/main/Serif/SubsetOTF/SC/NotoSerifSC-Regular.otf"
SIZE=28
OUT="fonts/font28cn.c"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== 导出界面实际用到的字符集 =="
python3 tools/check_charset.py --dump "$WORK/charset.txt"

echo "== 下载思源宋 =="
curl -sSL --retry 3 -o "$WORK/serif.otf" "$FONT_URL"

echo "== 生成 $OUT（${SIZE}px, 4bpp, 不压缩）=="
# --no-compress 是必须的：CONFIG_LV_FONT_COMPRESSED 在本工程是关的，
# 压缩过的字体运行时解不开，且不会有编译错误——只会渲染成空白。
npx --yes lv_font_conv@1.5.3 \
    --font "$WORK/serif.otf" \
    --size "$SIZE" \
    --bpp 4 \
    --format lvgl \
    --no-compress \
    --lv-include lvgl.h \
    --lv-font-name font28cn \
    --symbols "$(cat "$WORK/charset.txt")" \
    -o "$REPO/$OUT"

echo "== 完成：$(wc -c < "$REPO/$OUT") 字节源码 =="
echo "改过中文文案后要重跑本脚本，否则新字在标题里会是空白。"
