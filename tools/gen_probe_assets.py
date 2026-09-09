#!/usr/bin/env python3
"""生成素材样机（ui_probe）用的烘焙位图，输出 main/ui/probe_assets.c。

样机要回答的问题是「这块雾面电阻屏到底吃不吃得下拟物」，其中最关键的一条
对比是：**LVGL 实时画的 2-stop 渐变（必然有色带）** vs **离线抖动好再烘焙
成位图的渐变（色带被噪声打散）**。后者没法用 LVGL 的样式属性表达，只能预先
生成，所以有了这个脚本。

字节序不能想当然：工程开了 CONFIG_LV_COLOR_16_SWAP=y。查 lv_color.h 里
16 位带 swap 的位域顺序是 green_h(3) / red(5) / blue(5) / green_l(3)，
小端填充下 full == byteswap(标准 RGB565)，所以位图里每个像素要按
**大端**（高字节在前）写。写反了不会报错，只会看到红蓝错乱的一片。

用法：
    python3 tools/gen_probe_assets.py
"""
import os
import random

OUT = "main/ui/probe_assets.c"

PAPER = (0xEF, 0xE6, 0xD4)


def quant(c):
    """8 位每通道 -> RGB565 能表示的最近值，返回 (565 值, 还原成 8 位的三元组)。"""
    r, g, b = (max(0, min(255, int(round(v)))) for v in c)
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    v565 = (r5 << 11) | (g6 << 5) | b5
    back = (round(r5 * 255 / 31), round(g6 * 255 / 63), round(b5 * 255 / 31))
    return v565, back


def ramp(w, h, top, bot):
    """竖向渐变，返回 h 行 × w 列的 8 位浮点像素。"""
    return [[tuple(top[k] + (bot[k] - top[k]) * y / (h - 1) for k in range(3))
             for _ in range(w)] for y in range(h)]


def dither(px):
    """Floyd–Steinberg。就地把量化误差扩散给右/下的邻居。"""
    h, w = len(px), len(px[0])
    px = [[list(c) for c in row] for row in px]
    out = []
    for y in range(h):
        line = []
        for x in range(w):
            old = px[y][x]
            v565, back = quant(old)
            line.append(v565)
            err = [old[k] - back[k] for k in range(3)]
            for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    for k in range(3):
                        px[ny][nx][k] += err[k] * f
        out.append(line)
    return out


def flat(px):
    """不抖动，直接量化——用来复现 LVGL 实时渐变会有的那种色带。"""
    return [[quant(c)[0] for c in row] for row in px]


def noise_tile(size, sigma, seed):
    """纸纹：以纸底为中心加高斯噪声，再抖动量化。sigma 是 8 位灰阶上的标准差。"""
    rnd = random.Random(seed)
    px = [[tuple(PAPER[k] + rnd.gauss(0, sigma) for k in range(3)) for _ in range(size)]
          for _ in range(size)]
    return dither(px)


def emit(f, name, rows):
    h, w = len(rows), len(rows[0])
    f.write(f"\n/* {name}: {w}x{h}, RGB565 大端（LV_COLOR_16_SWAP=1） */\n")
    f.write(f"static const uint8_t {name}_map[] = {{\n")
    for row in rows:
        # 每个像素两字节，高字节在前
        bs = ", ".join(f"0x{v >> 8:02X}, 0x{v & 0xFF:02X}" for v in row)
        f.write(f"    {bs},\n")
    f.write("};\n")
    f.write(f"const lv_img_dsc_t {name} = {{\n"
            f"    .header.cf = LV_IMG_CF_TRUE_COLOR,\n"
            f"    .header.always_zero = 0,\n"
            f"    .header.reserved = 0,\n"
            f"    .header.w = {w},\n"
            f"    .header.h = {h},\n"
            f"    .data_size = {w * h * 2},\n"
            f"    .data = {name}_map,\n"
            f"}};\n")


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)

    GW, GH = 148, 36
    # 纸面浮雕会用到的明暗范围（纸底 -> 压深一档）
    paper_ramp = ramp(GW, GH, (0xEF, 0xE6, 0xD4), (0xC9, 0xBC, 0x9E))
    # 黄铜构件会用到的范围（亮铜 -> 暗铜）
    copper_ramp = ramp(GW, GH, (0xB8, 0x76, 0x3A), (0x6E, 0x3D, 0x0E))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write('/* 由 tools/gen_probe_assets.py 生成，请勿手改。 */\n')
        f.write('#include "probe_assets.h"\n')
        emit(f, "probe_grad_paper_flat", flat(paper_ramp))
        emit(f, "probe_grad_paper_dith", dither(paper_ramp))
        emit(f, "probe_grad_copper_flat", flat(copper_ramp))
        emit(f, "probe_grad_copper_dith", dither(copper_ramp))
        for i, sigma in enumerate((1.0, 2.0, 4.0, 7.0)):
            emit(f, f"probe_noise{i}", noise_tile(32, sigma, 1000 + i))

    print(f"已写出 {OUT}（{os.path.getsize(OUT)} 字节源码）")
    px = 4 * GW * GH + 4 * 32 * 32
    print(f"位图数据合计 {px * 2} 字节 ≈ {px * 2 / 1024:.1f} KB 闪存")


if __name__ == "__main__":
    main()
