#
# 共享字体组件。两个固件都用它，但 font20 分两份：
#
#   font20.c        原版，3888 个字形，编译后约 966KB。fmo-companion 用它
#                   （app_ui.c 里 LV_FONT_DECLARE(font20)），它的文案覆盖面
#                   大，裁剪风险高。
#
#   font20_cwtrn.c  按 cw-trainer 实际用到的 350 个字形裁剪，编译后约 54KB。
#                   由 tools/subset_font.py 从 font20.c 生成，字形数据逐像素
#                   一致，渲染不会变样。
#
# 两者定义同一个符号 font20，不能同时编译。按工程名二选一——省下的 900KB
# 正是 cw-trainer 能放下双槽 OTA 的前提。
#
# **改 cw-trainer 的中文文案后要重新生成**，否则新字会渲染成空白：
#     python3 tools/check_charset.py          # 构建前自检，发现表外字符报错
#     python3 tools/subset_font.py fonts/font20.c <字符集> fonts/font20_cwtrn.c
#
set(font_srcs
    "font28.c"
    "EN_BIG.c"
    "clock_font.c"
    "status_font.c"
)

if("${CMAKE_PROJECT_NAME}" STREQUAL "cw-trainer")
    list(APPEND font_srcs "font20_cwtrn.c")
    # 标题用的 28px 宋体。只有 cw-trainer 用，fmo-companion 不背这个重量。
    # 由 tools/gen_title_font.sh 从思源宋（OFL）生成，出处和命令都在脚本里
    # ——这是 fonts/ 下第一个有源可重生成的字体。
    list(APPEND font_srcs "font28cn.c")
else()
    list(APPEND font_srcs "font20.c")
endif()

idf_component_register(
    SRCS ${font_srcs}
    REQUIRES
        lvgl
)
