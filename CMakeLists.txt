cmake_minimum_required(VERSION 3.16)
# 本工程的版本号独立于仓库 tag：不设 PROJECT_VER 时 ESP-IDF 会取 git describe，
# 而这个仓库的 tag（V1.31）属于原厂 fmo-companion 那条线，两个固件共用 tag 命名空间。
# 固定在这里既保证发布版名副其实，也不会连累另一个固件的版本字符串。
set(PROJECT_VER "v1.4.3")

# 共享的 vendored LVGL 组件栈与字体源码存放在仓库根目录，供
# fmo-companion 与 cw-trainer 两个工程共用，避免各自再存一份。
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../components
    ${CMAKE_CURRENT_LIST_DIR}/../../fonts
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cw-trainer)
