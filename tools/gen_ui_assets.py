#!/usr/bin/env python3
"""生成正式界面用的平铺纹理，输出 main/ui/ui_assets.c。

和 gen_probe_assets.py 的区别：那个是一次性的量具，这个是发布固件的一部分，
所以进 git（脚本在这里，随时可重跑）。

只有平铺纹理，没有渐变——素材样机第 2 页实测：这块屏连离线 Floyd-Steinberg
抖动过的渐变都有可见台阶，所以风格收紧成「平涂 + 浮雕 + 纹理」。纹理本来就
是噪点，正好绕开这块屏最不擅长的事。

字节序：工程开了 CONFIG_LV_COLOR_16_SWAP=y，查 lv_color.h 的位域顺序
（green_h/red/blue/green_l）得出 full == byteswap(标准 RGB565)，所以位图每个
像素按**大端**写。写反了不报错，只会红蓝错乱。

用法：
    python3 tools/gen_ui_assets.py
"""
import os
import random

OUT = "firmware/cw-trainer/main/ui/ui_assets.c"
TILE = 32

PAPER = (0xEF, 0xE6, 0xD4)
BRASS = (0xA8, 0x76, 0x3C)


def quant(c):
    r, g, b = (max(0, min(255, int(round(v)))) for v in c)
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    v = (r5 << 11) | (g6 << 5) | b5
    return v, (round(r5 * 255 / 31), round(g6 * 255 / 63), round(b5 * 255 / 31))


def dither(px):
    """Floyd–Steinberg。噪声本身就不规则，但量化误差如果不扩散，
       低强度纹理会整片塌到同一个色阶上、等于没有纹理。"""
    h, w = len(px), len(px[0])
    px = [[list(c) for c in row] for row in px]
    out = []
    for y in range(h):
        line = []
        for x in range(w):
            old = px[y][x]
            v, back = quant(old)
            line.append(v)
            err = [old[k] - back[k] for k in range(3)]
            for dx, dy, f in ((1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    for k in range(3):
                        px[ny][nx][k] += err[k] * f
        out.append(line)
    return out


def grain(base, sigma, seed):
    """各向同性噪点：纸纤维。"""
    r = random.Random(seed)
    return dither([[tuple(base[k] + r.gauss(0, sigma) for k in range(3))
                    for _ in range(TILE)] for _ in range(TILE)])


# 这里本来是「拉丝」：每行取一个随机亮度偏移，模拟金属的方向性纹理。
# **真机上它变成了明显的横向台阶**，看着就像一段没做好的渐变。
#
# 原因是量化，不是噪声强度：黄铜 #A8763C 附近，8 位色差 2 个单位在 RGB565 里
# 红通道就跳一整级（量化后 #AD753A vs #A5753A）。行偏移 sigma=6 意味着每一行
# 都稳稳落在不同的量化档上，而「同一行内高度相关」恰恰让这一档铺满整行——
# 于是每行都是一条干净的色带，32px 瓦片再一平铺，横条就规律地重复出来。
#
# 教训：**在 16 位色上，任何行/列相干的低幅变化都会被量化放大成条带。**
# 要材质就得用各向同性的噪点，让抖动有机会把误差散开——纸纹一直没问题，
# 正是因为它本来就是各向同性的。


def brushed(base, seed):
    """黄铜的细颗粒。曾经是横向拉丝，为什么放弃见上方注释。"""
    return grain(base, 3.0, seed)


def emit(f, name, rows, note):
    h, w = len(rows), len(rows[0])
    f.write(f"\n/* {name}: {w}x{h} 可平铺。{note} */\n")
    f.write(f"static const uint8_t {name}_map[] = {{\n")
    for row in rows:
        f.write("    " + ", ".join(f"0x{v >> 8:02X}, 0x{v & 0xFF:02X}" for v in row) + ",\n")
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
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    with open(OUT, "w", encoding="utf-8") as f:
        f.write('/* 由 tools/gen_ui_assets.py 生成，请勿手改。 */\n')
        f.write('#include "ui_assets.h"\n')
        # sigma 4 而不是样机上选中的 7：7 是在「装饰区」这个前提下选的，
        # 而页面底纹整片都压着正文，实测 7 会让中文略微变难读。
        emit(f, "ui_grain_paper", grain(PAPER, 4.0, 4001),
             "纸纤维 sigma=4。页面底纹整片压着正文，用密集文字区那一档")
        emit(f, "ui_grain_brass", brushed(BRASS, 4002),
             "黄铜细颗粒 sigma=3。只用在电键页的桨上")
    n = 2 * TILE * TILE * 2
    print(f"已写出 {OUT}，位图数据 {n} 字节 ≈ {n/1024:.1f} KB")


if __name__ == "__main__":
    main()
