#
# 字体组件。四个字体源码，编译后约 1.1MB 进 app：
#
#   font20_cwtrn.c  界面正文。按实际用到的 368 个字形裁剪，编译后约 54KB。
#                   由 tools/subset_font.py 从全量字库 tools/font-src/font20.c
#                   （3888 个字形，编译后约 966KB）生成，字形数据逐像素一致，
#                   渲染不会变样。它定义的符号就叫 font20。
#
#                   省下的 900KB 正是这个固件能放下双槽 OTA 的前提，所以全量
#                   字库只作裁剪原料、不参与编译，放在 tools/font-src/ 下。
#
#   font28.c        28px 英文/数字，状态栏与大字号读数
#   font28cn.c      28px 标题宋体，由 tools/gen_title_font.sh 从思源宋（OFL）
#                   生成——出处和命令都在脚本里，这是唯一有源可重生成的字体
#   EN_BIG.c        大号英文，练习页的字符提示
#
# **改中文文案后要重新裁剪**，否则新字会渲染成空白：
#     python3 tools/check_charset.py     # 构建前自检，发现表外字符直接报错
#     python3 tools/subset_font.py tools/font-src/font20.c <字符集> \
#             components/fonts/font20_cwtrn.c
#
# 字体来源与许可见仓库根的 NOTICE.md。
#
idf_component_register(
    SRCS
        "font20_cwtrn.c"
        "font28.c"
        "font28cn.c"
        "EN_BIG.c"
    REQUIRES
        lvgl
)
