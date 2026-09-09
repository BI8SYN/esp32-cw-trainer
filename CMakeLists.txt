cmake_minimum_required(VERSION 3.16)
# 版本号写在这里，不取 git describe：它被嵌进 bin，设备的设置页和启动日志显示的
# 都是它。CI 在打 tag 时会校验 tag 名与这个值一致（见 .github/workflows/release.yml），
# 所以两者不会走散。
set(PROJECT_VER "v1.5.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cw-trainer)
