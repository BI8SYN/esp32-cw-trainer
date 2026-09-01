# 素材样机：只在 idf.py -DCW_STYLE_PROBE=ON 时编进来。
#
# 它带着约 50KB 的烘焙位图（见 tools/gen_probe_assets.py），发布版里没有理由
# 背这个重量；而它要回答的问题（雾面屏吃不吃得下拟物）又只有真机能回答，
# 所以做成一个开关，而不是删掉重写。
option(CW_STYLE_PROBE "编入拟物素材样机页面" OFF)
set(probe_srcs "")
if(CW_STYLE_PROBE)
    list(APPEND probe_srcs "ui/ui_probe.c" "ui/probe_assets.c")
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