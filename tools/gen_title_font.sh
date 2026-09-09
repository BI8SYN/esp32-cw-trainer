#!/usr/bin/env bash
# 重新生成标题字体 components/fonts/font28cn.c。
#
# ── 为什么会有这个脚本 ──
#
# 其余的 font20.c / font28.c / EN_BIG.c 都是第三方「Lvgl Font Tool
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
# ── 为什么只裁标题用到的那十几个字 ──
#
# 第一版留了完整的 368 字形，理由是「写了新标题却渲染成空白」是这个项目已经踩过
# 的坑。代价是 **123 KB，而标题字体全工程只用在一处**（首页的「KOCH 第 N 课」），
# 占固件的 8%。
#
# 现在不需要那个代价了：LVGL 8.3 的 lv_font_t 支持 fallback（lv_font.c 里递归
# 解析），把它接到 font20 之后，**缺字的失败模式从「空白」变成「退成 20px」**
# ——字号不对但看得见。而 tools/check_charset.py 已经保证 font20 覆盖全部界面
# 文案，所以这两条合起来是一个硬保证：**标题字体想裁多小，都不可能渲染成空白。**
#
# 加新标题时如果字不在下面这行里，屏幕上会是 20px 的小字——看得出来，改一下
# TITLE_CHARS 重跑本脚本即可。（实测验证过：把「练」放进标题、不加进 TITLE_CHARS，
# 屏幕上它确实退成了小字，不是空白。）
#
# **注意 TITLE_CHARS 会影响版式。** lv_font_conv 的 line_height / base_line 是按
# 实际收录的字形算出来的：完整字符集时是 32/7，裁到现在这 17 个字形之后变成
# 26/2。所以往里加一个更高或更深的字，标题的行高会跟着跳、位置会移——改完记得
# 用指纹巡检看一眼首页有没有变（tools/flash_remote.sh --monitor）。
#
# 依赖：node + npx（会临时装 lv_font_conv）、curl
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"

FONT_URL="https://github.com/notofonts/noto-cjk/raw/main/Serif/SubsetOTF/SC/NotoSerifSC-Regular.otf"
SIZE=28
OUT="components/fonts/font28cn.c"
FALLBACK="font20"

# 标题实际用到的字符。当前唯一的使用点是 ui_home.c 的「KOCH 第 %u 课」。
# grep UI_FONT_TITLE 可以找到全部使用点。
TITLE_CHARS='KOCH第课0123456789 '


WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== 下载思源宋 =="
curl -sSL --retry 3 -o "$WORK/serif.otf" "$FONT_URL"

echo "== 生成 ${OUT} （${SIZE}px, 4bpp, 不压缩）=="
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
    --lv-fallback "$FALLBACK" \
    --symbols "$TITLE_CHARS" \
    -o "$REPO/$OUT"

echo "== 完成：$(wc -c < "$REPO/$OUT") 字节源码 =="
echo "标题里出现 TITLE_CHARS 之外的字时，会退回 $FALLBACK 渲染（20px，看得出来）。"
echo "要恢复大字号，把那个字加进 TITLE_CHARS 再重跑本脚本。"
