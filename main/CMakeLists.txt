# 素材样机：只在 idf.py -DCW_STYLE_PROBE=ON 时编进来。
#
# 它带着约 50KB 的烘焙位图（见 tools/gen_probe_assets.py），发布版里没有理由
# 背这个重量；而它要回答的问题（雾面屏吃不吃得下拟物）又只有真机能回答，
# 所以做成一个开关，而不是删掉重写。
option(CW_STYLE_PROBE "编入拟物素材样机页面" OFF)
set(probe_srcs "")
if(CW_STYLE_PROBE)
    # probe_assets.c 是生成物、不进 git（见 .gitignore），所以新克隆的仓库里
    # 它不存在。缺了直接 list() 进去，CMake 只会报一句「找不到源文件」，看不出
    # 该怎么办——这里替调用者跑一次生成脚本。脚本是纯函数，重跑无副作用。
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/ui/probe_assets.c")
        message(STATUS "生成样机位图：tools/gen_probe_assets.py")
        execute_process(
            COMMAND python3 "${CMAKE_CURRENT_LIST_DIR}/../tools/gen_probe_assets.py"
            RESULT_VARIABLE gen_rc
            OUTPUT_VARIABLE gen_out)
        if(NOT gen_rc EQUAL 0)
            message(FATAL_ERROR "生成样机位图失败（rc=${gen_rc}）：${gen_out}")
        endif()
    endif()
    list(APPEND probe_srcs "ui/ui_probe.c" "ui/probe_assets.c")
endif()

# 画面指纹：开机自动把每个页面建一遍、对刷向屏幕的像素算指纹打到串口。
# 用来给「不该改变观感」的重构做验收，见 screenshot.c 顶部说明。
option(CW_SCREENSHOT "编入画面指纹巡检" OFF)
# 导图：除指纹外把像素 base64 出串口，主机端拼 PNG。慢（每页约 18 秒），
# 只在需要肉眼确认长相时开。
option(CW_SCREENSHOT_DUMP "巡检时同时导出图像" OFF)
if(CW_SCREENSHOT)
    list(APPEND probe_srcs "screenshot.c")
endif()

idf_component_register(
    SRCS
        "main.c"
        "cw_settings.c"
        "tone_gen.c"
        "cw_player.c"
        "key_input.c"
        "core/morse_table.c"
        "core/morse_timing.c"
        "core/keyer.c"
        "core/key_merge.c"
        "core/version_cmp.c"
        "core/tone_seq.c"
        "core/cw_decoder.c"
        "core/koch_engine.c"
        "core/callsign_gen.c"
        "core/trainer.c"
        "core/kob_packet.c"
        "core/kob_client.c"
        "core/kob_rxbuf.c"
        "core/cw_stats.c"
        "net_store.c"
        "stats_store.c"
        "wifi_net.c"
        "time_sync.c"
        "kob_net.c"
        "ota.c"
        "ui_async.c"
        "backlight.c"
        "battery.c"
        "ui/ui_common.c"
        "ui/ui_sound.c"
        "ui/ui_assets.c"
        "ui/ui_icons.c"
        "ui/ui_home.c"
        "ui/ui_preview.c"
        "ui/ui_practice.c"
        "ui/ui_result.c"
        "ui/ui_keyer.c"
        "ui/ui_settings.c"
        "ui/ui_callsign.c"
        "ui/ui_wifi.c"
        "ui/ui_net.c"
        "ui/ui_ota.c"
        "ui/ui_more.c"
        "ui/ui_stats.c"
        "ui/ui_fist.c"
        ${probe_srcs}
    INCLUDE_DIRS "." "core" "ui"
    REQUIRES
        esp_app_format
        esp_wifi
        esp_netif
        esp_event
        lwip
        app_update
        esp_http_client
        esp_https_ota
        driver
        esp_driver_i2s
        esp_timer
        nvs_flash
        esp_adc
        lvgl
        lv_porting
        lvgl_esp32_drivers
        fonts
)

if(CW_STYLE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CW_STYLE_PROBE=1)
endif()
if(CW_SCREENSHOT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CW_SCREENSHOT=1)
    if(CW_SCREENSHOT_DUMP)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE CW_SCREENSHOT_DUMP=1)
    endif()
endif()
