#!/usr/bin/env python3
"""检查 ui_theme.h 的色板在 RGB565 上还站不站得住。

**为什么需要这个：** 设计稿上不同的两个颜色，量化到 16 位色可能变成同一个值。
实测踩过一次——UI_HILITE #FFF2D7 和 UI_RAISE #FDF0D6 的红通道都落进 5bit 的
最后一档，量化后完全相同，**凸起按钮的高光边等于没画**。这个错误编译不报、
指纹只说「变了」、看缩略图也看不出来，只有逐像素量才发现。

两项检查：
  1. 撞车：任意两个 token 量化后同值（pressed/sunk 是刻意同值，白名单放行）
  2. 对比度：正文色压在各个面上是否满足 4.5:1

用法：
    python3 tools/check_palette.py
"""
import re
import sys

THEME = "main/ui/ui_theme.h"

# 刻意同值、只是角色不同的组合。**这些不是撞车**——把「今天恰好相同」和
# 「本来就该相同」分开，正是这套 token 按角色命名的初衷（见 ui_theme.h）。
ALLOWED_SAME = [
    {"UI_SUNK", "UI_PRESSED"},     # 按下就是抬高面塌成凹陷面
    {"UI_INK2", "UI_DATA"},        # 次要文字 / 图表柱子
    {"UI_ACCENT", "UI_FILL"},      # 主操作 / 进度填充
    {"UI_RULE", "UI_GRID"},        # 组内分割线 / 图表网格
]

# 硬性检查：UI_INK 是屏幕的默认文字色，会继承到几乎所有控件，因此它压在**任何
# 一个面**上都必须达标。其余组合（次要墨、铜色、危险色压在各个面上）只在少数
# 地方出现，全查会产出一堆界面里根本不存在的组合——**会误报的检查等于训练人
# 忽略它**，所以那些只做提示，不算失败。
SURFACES = ["UI_PAPER", "UI_RAISE", "UI_SUNK", "UI_SELECTED"]
INKS_REQUIRED = ["UI_INK"]
INKS_ADVISORY = ["UI_INK2", "UI_ACCENT", "UI_DANGER"]
MIN_RATIO = 4.5


def q565(v):
    r, g, b = (v >> 16) & 255, (v >> 8) & 255, v & 255
    return ((round((r >> 3) * 255 / 31) << 16)
            | (round((g >> 2) * 255 / 63) << 8)
            | round((b >> 3) * 255 / 31))


def lum(v):
    def f(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * f((v >> 16) & 255) + 0.7152 * f((v >> 8) & 255) + 0.0722 * f(v & 255)


def ratio(a, b):
    la, lb = lum(q565(a)), lum(q565(b))
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def main():
    src = open(THEME, encoding="utf-8").read()
    pal = {m.group(1): int(m.group(2), 16)
           for m in re.finditer(r"#define\s+(UI_\w+)\s+lv_color_hex\(0x([0-9A-Fa-f]{6})\)", src)}
    if not pal:
        print("没在 ui_theme.h 里解析到任何颜色", file=sys.stderr)
        return 1
    print(f"解析到 {len(pal)} 个颜色 token")

    bad = 0
    groups = {}
    for name, v in pal.items():
        groups.setdefault(q565(v), []).append(name)
    for qv, names in sorted(groups.items()):
        if len(names) < 2:
            continue
        if any(set(names) <= allow for allow in ALLOWED_SAME):
            continue
        print(f"  撞车：{' / '.join(names)} 量化后都是 #{qv:06X}")
        bad += 1

    for s in SURFACES:
        for i in INKS_REQUIRED:
            if s not in pal or i not in pal:
                continue
            r = ratio(pal[i], pal[s])
            if r < MIN_RATIO:
                print(f"  对比度不足：{i} 压在 {s} 上只有 {r:.2f}:1")
                bad += 1

    advisory = []
    for s in SURFACES:
        for i in INKS_ADVISORY:
            if s not in pal or i not in pal:
                continue
            r = ratio(pal[i], pal[s])
            if r < MIN_RATIO:
                advisory.append(f"  别把 {i} 放到 {s} 上（{r:.2f}:1）")
    if advisory:
        print("提示（不算失败，只是别这么用）：")
        print("\n".join(advisory))

    print("色板检查通过。" if not bad else f"发现 {bad} 处问题。")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
