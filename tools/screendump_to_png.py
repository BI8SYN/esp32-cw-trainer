#!/usr/bin/env python3
"""把 CW_SCREENSHOT_DUMP 的串口输出拼回 PNG。

固件端（main/screenshot.c）在 flush_cb 里把每个刷新块打成：

    CHUNK x1 y1 x2 y2
    <base64 的 RGB565 像素>

字节序：工程开了 CONFIG_LV_COLOR_16_SWAP=y，缓冲里每个像素是**大端**的
标准 RGB565（高字节在前）。按小端读会红蓝错乱。

用法：
    python3 tools/screendump_to_png.py <串口日志> <输出目录>
"""
import base64
import re
import struct
import sys
import zlib

W, H = 320, 240


def write_png(path, rgb, w, h):
    """最小 PNG 编码器。不引入 Pillow 依赖——这个脚本要在任何装了 python3
       的机器上都能跑，而它做的事只是把已知尺寸的 RGB 字节写成文件。"""
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    log, outdir = sys.argv[1], sys.argv[2]
    lines = open(log, encoding="utf-8", errors="replace").read().split("\n")

    name = None
    canvas = bytearray(W * H * 3)
    pending = None      # (x1,y1,x2,y2)
    b64 = []
    done = []

    def flush_chunk():
        if pending is None:
            return
        x1, y1, x2, y2 = pending
        # 串口不是可靠传输：115200 上偶尔会掉几个字符（实测 10739 行里坏 7 行）。
        # 与其为此加一套重传，不如让解码容错——坏掉的那几行只影响几十个像素，
        # 而这张图是用来看版式和配色的，不是用来做像素比对的（那是指纹的活）。
        raw = "".join(b64)
        raw = raw[:len(raw) // 4 * 4]
        data = base64.b64decode(raw, validate=False)
        cw = x2 - x1 + 1
        for i in range((x2 - x1 + 1) * (y2 - y1 + 1)):
            if (i + 1) * 2 > len(data):
                break
            v = (data[i * 2] << 8) | data[i * 2 + 1]     # 大端
            r = ((v >> 11) & 0x1F) * 255 // 31
            g = ((v >> 5) & 0x3F) * 255 // 63
            b = (v & 0x1F) * 255 // 31
            x, y = x1 + i % cw, y1 + i // cw
            if 0 <= x < W and 0 <= y < H:
                o = (y * W + x) * 3
                canvas[o:o + 3] = bytes((r, g, b))

    for ln in lines:
        m = re.match(r"SNAP (\w+)\s+px=", ln)
        if m:
            # CHUNK 行在 SNAP 行**之前**——指纹是整屏重绘完才打的。所以此刻
            # 攒着的像素属于这一行报出的这个页面，不是上一个。
            flush_chunk()
            pending, b64 = None, []
            name = m.group(1)
            write_png(f"{outdir}/{name}.png", bytes(canvas), W, H)
            done.append(name)
            canvas = bytearray(W * H * 3)
            continue
        m = re.match(r"CHUNK (-?\d+) (-?\d+) (-?\d+) (-?\d+)", ln)
        if m:
            flush_chunk()
            pending = tuple(int(g) for g in m.groups())
            b64 = []
            continue
        if pending is not None:
            t = ln.strip()
            if t and re.fullmatch(r"[A-Za-z0-9+/=]+", t):
                b64.append(t.rstrip("="))

    print("已写出：" + ", ".join(f"{n}.png" for n in done))


if __name__ == "__main__":
    main()
