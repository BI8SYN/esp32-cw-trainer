# 本地改动（见仓库根 NOTICE.md）：原始 glob 无条件把 demos/*.c 也编进来，
# 而这个仓库不带 LVGL 的 demos/examples——它们只在官方示例里用得到，却占了
# 上游仓库 68MB 中的大头。同时删掉下面按 CONFIG_LV_USE_DEMO_* 追加 demo 源码
# 的分支，以及 examples 的 include 目录。
file(GLOB_RECURSE SOURCES ${LVGL_ROOT_DIR}/src/*.c)

idf_build_get_property(LV_MICROPYTHON LV_MICROPYTHON)

if(LV_MICROPYTHON)
  idf_component_register(
    SRCS
    ${SOURCES}
    INCLUDE_DIRS
    ${LVGL_ROOT_DIR}
    ${LVGL_ROOT_DIR}/src
    ${LVGL_ROOT_DIR}/demos
    ${LVGL_ROOT_DIR}/../
    REQUIRES
    main)
else()
  idf_component_register(SRCS ${SOURCES}
      INCLUDE_DIRS ${LVGL_ROOT_DIR} ${LVGL_ROOT_DIR}/src ${LVGL_ROOT_DIR}/../
      REQUIRES esp_timer)
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLV_CONF_INCLUDE_SIMPLE")

if(CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM)
  target_compile_definitions(${COMPONENT_LIB}
                             PUBLIC "-DLV_ATTRIBUTE_FAST_MEM=IRAM_ATTR")
endif()
