#!/usr/bin/env python3
"""检查 cw-trainer 的界面文案有没有用到裁剪字体之外的字符。

font20_cwtrn.c 只含界面实际用到的那几百个字形（见 fonts/CMakeLists.txt）。
新增中文文案时若用到表外的字，**不会有编译错误，只会在屏幕上渲染成空白**
——这是那种只有对着真屏幕才看得出来的问题，必须在构建前拦下。

用法：
    python3 tools/check_charset.py              # 检查，有问题返回非 0
    python3 tools/check_charset.py --dump FILE  # 顺便导出完整字符集，
                                                # 喂给 subset_font.py 重新生成
"""
import glob
import re
import sys

SRC_GLOBS = [
    "firmware/cw-trainer/main/**/*.c",
    "firmware/cw-trainer/main/**/*.h",
]
FONT_C = "fonts/font20_cwtrn.c"

# 运行时还会显示这些，源码里不一定以字面量出现：莫尔斯字符表、呼号、
# 数字、百分号等。
RUNTIME_EXTRA = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?.,=+-"


def used_chars():
    used = set(RUNTIME_EXTRA)
    for pat in SRC_GLOBS:
        for path in sorted(glob.glob(pat, recursive=True)):
            text = open(path, encoding="utf-8").read()
            # 注释里的字不会显示出来，不该被计入
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
                for ch in m.group(1):
                    if ord(ch) >= 0x20:
                        used.add(ch)
    return used


def font_chars():
    src = open(FONT_C, encoding="utf-8").read()
    body = src[src.index("font_cp[] = {"):]
    body = body[:body.index("};")]
    return set(chr(int(m, 16)) for m in re.findall(r"0x([0-9A-Fa-f]{4})", body))


def main():
    used = used_chars()
    have = font_chars()
    missing = sorted(used - have)

    if "--dump" in sys.argv:
        out = sys.argv[sys.argv.index("--dump") + 1]
        open(out, "w", encoding="utf-8").write("".join(sorted(used)))
        print("字符集已导出到 %s（%d 个）" % (out, len(used)))

    print("界面用到 %d 个字符，裁剪字体含 %d 个" % (len(used), len(have)))
    if missing:
        print("\n以下字符不在裁剪字体里，屏幕上会是空白：")
        print("  " + "".join(missing))
        print("\n重新生成字体：")
        print("  python3 tools/check_charset.py --dump /tmp/cs.txt")
        print("  python3 tools/subset_font.py fonts/font20.c /tmp/cs.txt fonts/font20_cwtrn.c")
        return 1
    print("全部命中。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
