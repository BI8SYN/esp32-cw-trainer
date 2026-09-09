#!/usr/bin/env python3
"""裁剪 Lvgl Font Tool（XBF 格式）生成的字体 .c，只保留用到的字形。

为什么不用 lv_font_conv：那个工具是 TTF -> C，而仓库里**没有 font20 的源
字体**（只有已生成的 .c）。本工具是 C -> C，直接在现有字形数据上裁剪，
好处是渲染效果与原字体逐像素一致，不会因为换字体而变样。

原格式（见 font20.c 顶部的结构体定义）：

    x_header_t { uint16 min; uint16 max; uint8 bpp; uint8 reserved[3]; }
    x_table_t  { uint32 pos; }  ×  (max - min + 1)      <- 按码点直接索引
    然后是各字形： glyph_dsc_t{adv_w,box_w,box_h,ofs_x,ofs_y,r} + 位图

**偏移表是最大的一块浪费**：font20 覆盖 0x09~0xffe5，表就有 65501 项、
262KB，和实际有多少字形完全无关。只删字形省不掉它。

所以输出改用紧凑格式：升序码点数组 + 平行偏移数组 + 二分查找。访问函数
本来就在同一个 .c 里由工具生成，换格式不影响任何调用方。

用法：
    python3 tools/subset_font.py tools/font-src/font20.c <字符集文件> components/fonts/font20_cwtrn.c
"""
import re
import sys


def parse_c_bytes(path):
    """把 .c 里 glyph_dsc_xbf[] 的内容还原成 bytes。十进制和 0x 混排。"""
    src = open(path, encoding="utf-8", errors="replace").read()
    start = src.index("glyph_dsc_xbf[] = {")
    start = src.index("{", start) + 1
    end = src.index("\n};", start)
    body = src[start:end]
    # 必须先剥行注释再剥块注释：位图的字符画里有 "//*@+" 这样的行，
    # "//" 后面紧跟 "*" 就构成了 "/*"，先剥块注释会从这里一路吞到下一个
    # "*/"，把中间的真实数据整段吃掉（实测少解析 250KB）。
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    out = bytearray()
    for tok in re.findall(r"0x[0-9a-fA-F]+|\d+", body):
        out.append(int(tok, 16) if tok.startswith("0x") else int(tok))
    return bytes(out)


def main():
    src_c, charset_path, out_c = sys.argv[1], sys.argv[2], sys.argv[3]

    raw = parse_c_bytes(src_c)
    lo = raw[0] | (raw[1] << 8)
    hi = raw[2] | (raw[3] << 8)
    bpp = raw[4]
    n_slots = hi - lo + 1
    table_off = 8
    print("原字体: 码点 0x%04X~0x%04X  bpp=%d  偏移表 %d 项 (%.1f KB)  总计 %.1f KB"
          % (lo, hi, bpp, n_slots, n_slots * 4 / 1024, len(raw) / 1024))

    def slot(u):
        p = table_off + (u - lo) * 4
        return (raw[p] | (raw[p + 1] << 8) | (raw[p + 2] << 16) | (raw[p + 3] << 24))

    wanted = sorted(set(open(charset_path, encoding="utf-8").read()))
    wanted = [c for c in wanted if ord(c) >= 0x20]

    blob = bytearray()
    cps, offs = [], []
    missing = []
    for c in wanted:
        u = ord(c)
        if u < lo or u > hi:
            missing.append(c)
            continue
        pos = slot(u)
        if pos == 0:
            missing.append(c)
            continue
        adv_w, box_w, box_h = raw[pos], raw[pos + 1], raw[pos + 2]
        bmp_len = ((box_w * bpp + 7) // 8) * box_h
        rec = raw[pos:pos + 6 + bmp_len]
        cps.append(u)
        offs.append(len(blob))
        blob += rec

    if missing:
        print("警告：原字体里没有这些字符，将渲染为空白: %s" % "".join(missing))

    # 生成紧凑格式
    lines = []
    w = lines.append
    w("/*")
    w(" * 由 tools/subset_font.py 从原始 %s 裁剪而来。" % src_c.split("/")[-1])
    w(" * 字形数据逐像素来自原字体，渲染效果不变。")
    w(" *")
    w(" * 原格式按「码点 - min」直接索引一张 %d 项的偏移表（%.0f KB），" % (n_slots, n_slots * 4 / 1024))
    w(" * 与实际字形数无关。这里改为「升序码点数组 + 平行偏移数组 + 二分查找」，")
    w(" * 表的大小随字形数走。")
    w(" *")
    w(" * 只包含界面实际用到的 %d 个字形。**新增中文文案时若用到表外的字，" % len(cps))
    w(" * 会渲染成空白**——tools/check_charset.py 会在构建前拦下这种情况。")
    w(" */")
    w('#include "lvgl.h"')
    w("")
    w("typedef struct {")
    w("    uint8_t adv_w;")
    w("    uint8_t box_w;")
    w("    uint8_t box_h;")
    w("    int8_t  ofs_x;")
    w("    int8_t  ofs_y;")
    w("    uint8_t r;")
    w("} glyph_dsc_t;")
    w("")
    w("#define FONT_BPP %d" % bpp)
    w("")

    w("static const uint16_t font_cp[] = {")
    for i in range(0, len(cps), 12):
        w("    " + " ".join("0x%04X," % c for c in cps[i:i + 12]))
    w("};")
    w("")
    w("static const uint32_t font_off[] = {")
    for i in range(0, len(offs), 8):
        w("    " + " ".join("%d," % o for o in offs[i:i + 8]))
    w("};")
    w("")
    w("#define FONT_N (sizeof font_cp / sizeof font_cp[0])")
    w("")
    w("static const uint8_t font_blob[] = {")
    for i in range(0, len(blob), 16):
        w("    " + " ".join("0x%02X," % b for b in blob[i:i + 16]))
    w("};")
    w("")
    w("/* 码点升序，二分查找。找不到返回 -1。 */")
    w("static int font_find(uint32_t u)")
    w("{")
    w("    int lo = 0, hi = (int)FONT_N - 1;")
    w("    while (lo <= hi) {")
    w("        int mid = (lo + hi) / 2;")
    w("        if (font_cp[mid] == u) return mid;")
    w("        if (font_cp[mid] < u) lo = mid + 1;")
    w("        else hi = mid - 1;")
    w("    }")
    w("    return -1;")
    w("}")
    w("")
    w("static const uint8_t *__user_font_get_bitmap(const lv_font_t *font, uint32_t unicode_letter)")
    w("{")
    w("    (void)font;")
    w("    int i = font_find(unicode_letter);")
    w("    if (i < 0) return NULL;")
    w("    return &font_blob[font_off[i] + sizeof(glyph_dsc_t)];")
    w("}")
    w("")
    w("static bool __user_font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,")
    w("                                     uint32_t unicode_letter, uint32_t unicode_letter_next)")
    w("{")
    w("    (void)font; (void)unicode_letter_next;")
    w("    int i = font_find(unicode_letter);")
    w("    if (i < 0) return false;")
    w("    const glyph_dsc_t *g = (const glyph_dsc_t *)&font_blob[font_off[i]];")
    w("    dsc_out->adv_w = g->adv_w;")
    w("    dsc_out->box_w = g->box_w;")
    w("    dsc_out->box_h = g->box_h;")
    w("    dsc_out->ofs_x = g->ofs_x;")
    w("    dsc_out->ofs_y = g->ofs_y;")
    w("    dsc_out->bpp   = FONT_BPP;")
    w("    return true;")
    w("}")
    w("")
    w("lv_font_t font20 = {")
    w("    .get_glyph_bitmap = __user_font_get_bitmap,")
    w("    .get_glyph_dsc = __user_font_get_glyph_dsc,")
    w("    .line_height = 26,")
    w("    .base_line = 0,")
    w("};")
    w("")

    open(out_c, "w", encoding="utf-8").write("\n".join(lines))
    total = len(blob) + len(cps) * 2 + len(offs) * 4
    print("裁剪后: %d 个字形  索引 %.1f KB  字形数据 %.1f KB  合计 %.1f KB"
          % (len(cps), (len(cps) * 6) / 1024, len(blob) / 1024, total / 1024))
    print("写出: %s" % out_c)


if __name__ == "__main__":
    main()
