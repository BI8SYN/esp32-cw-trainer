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

# 开发专用、发布版根本不编进去的文件（见 main/CMakeLists.txt 里的 CW_STYLE_PROBE
# / CW_SCREENSHOT 开关）。它们里的字符串不会出现在发布固件的屏幕上，算进来只会
# 白白撑大裁剪字库——实测 screenshot.c 里一句中文调试 printf 就能让自检报错、
# 逼着往字库里加字。字库大小不是小事：裁剪前固件 2.2MB，双 OTA 槽放不下。
DEV_ONLY = ("screenshot.c", "ui/ui_probe.c", "ui/probe_assets.c")
FONT_C = "fonts/font20_cwtrn.c"

# 运行时还会显示这些，源码里不一定以字面量出现：莫尔斯字符表、呼号、
# 数字、百分号等。
RUNTIME_EXTRA = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?.,=+-"


# 这些调用的参数是**诊断文本**：串口日志、编译期断言消息。它们是字符串字面量，
# 但永远不会出现在屏幕上——算进来只会让裁剪字库白白变大。而字库大小不是小事：
# 裁剪前固件 2.2MB，两个 OTA 槽根本放不下（见 main/ota.h）。
#
# 实测被它们多算进来的：_Static_assert 的中文消息带进「冻布局改」，
# ota.c 的一条 ESP_LOGW 带进「析」和破折号。
DIAGNOSTIC_CALLS = re.compile(r"\b(ESP_LOG[EWIDV]|ESP_EARLY_LOG[EWIDV]|_Static_assert)\s*\(")


def strip_diagnostics(text):
    """删掉诊断调用的整个参数表。

    不能用正则一把梭：参数里就有括号和引号（"服务器返回 %d (%s)"）。手写扫描，
    跟踪字符串和转义，数到配对的右括号为止。
    """
    out, i = [], 0
    while True:
        m = DIAGNOSTIC_CALLS.search(text, i)
        if not m:
            out.append(text[i:])
            return "".join(out)
        out.append(text[i:m.start()])
        j, depth, instr, esc = m.end(), 1, False, False
        while j < len(text) and depth:
            c = text[j]
            if esc:
                esc = False
            elif c == "\\" and instr:
                esc = True
            elif c == '"':
                instr = not instr
            elif not instr:
                if c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
            j += 1
        i = j


def used_chars():
    used = set(RUNTIME_EXTRA)
    for pat in SRC_GLOBS:
        for path in sorted(glob.glob(pat, recursive=True)):
            if any(path.endswith(d) for d in DEV_ONLY):
                continue
            text = open(path, encoding="utf-8").read()
            # 注释里的字不会显示出来，不该被计入
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            text = strip_diagnostics(text)
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
