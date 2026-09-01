#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "cw_settings.h"
#include "net_store.h"
#include "stats_store.h"
#include "ota.h"
#include "wifi_net.h"
#include "time_sync.h"
#include "tone_gen.h"
#include "cw_player.h"
#include "key_input.h"
#include "backlight.h"
#include "battery.h"
#include "ui_async.h"
#include "ui_common.h"
#include "ui_home.h"
#include "morse_timing.h"
#if CW_SCREENSHOT
#include "screenshot.h"
#endif

static const char *TAG = "main";

/*
 * 开机提示音的速度，以及播完之后要还回去的用户速度。
 *
 * 提示音是用 20 WPM 播的，播完必须把播放器的节奏还原成用户设的速度，否则
 * 之后所有的听写、收报回放都会按 20 WPM 走——那是用户完全没要求过的改变。
 */
#define BOOT_CHIME_WPM 20
static morse_timing_t s_user_timing;

static void boot_chime_done(cw_player_event_t ev, char c, void *user)
{
    (void)c; (void)user;
    if (ev == CW_PLAYER_EV_CHAR_START) return;   /* 只关心播完/被打断 */
    cw_player_set_timing(&s_user_timing);
}

static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

static void battery_ui(void *arg) { ui_home_set_battery((uint8_t)(uintptr_t)arg); }
static void battery_cb(uint8_t percent, uint16_t mv) { (void)mv; ui_async_post(battery_ui, (void *)(uintptr_t)percent); }

void app_main(void)
{
    esp_log_level_set("XPT2046", ESP_LOG_WARN);
    /* 必须在任何可能调用 ui_async_post() 的任务启动之前就绪——key_input_init()
     * 马上就会在 core 1 上拉起电键任务，battery_start()/cw_player 的 esp_timer
     * 回调也都会走这条路径。见 ui_async.h/ui_async.c 里的说明。 */
    ui_async_init();
    nvs_init();
    /* 必须尽早调用：如果本次是升级后第一次启动，不标记为可用的话
     * bootloader 会在下次复位时回滚到旧槽。放在 nvs 之后、UI 之前——
     * 能走到这里就说明基本功能是好的。 */
    ota_mark_valid();
    /* 不能用 ESP_ERROR_CHECK：NVS 分区与 fmo-companion 共享（见 docs/build-setup.md
     * 与本项目 README），对方写满时这里的读/写会失败。set_defaults() 已经在
     * cw_settings_init() 的每条失败分支里把整个结构体填成默认值，固件仍能正常
     * 运行，只是这一次没能把配置落盘持久化——降级为打印错误日志继续跑，而不是
     * abort。abort 会让设备在完全没有 UI 的情况下卡进"写失败 -> abort -> 重启 ->
     * 再次写失败"的无限重启循环，文档里写的 erase_region 恢复方法根本没有机会
     * 被用户看到、发现。 */
    esp_err_t settings_err = cw_settings_init();
    if (settings_err != ESP_OK) {
        ESP_LOGE(TAG, "cw_settings_init failed: %s - running with in-memory defaults, "
                 "settings will NOT persist across reboot", esp_err_to_name(settings_err));
    }
    const cw_settings_t *cfg = cw_settings_get();
    ESP_LOGI(TAG, "cfg: wpm=%u/%u tone=%u lesson=%u rot180=%u reset=%d",
             cfg->char_wpm, cfg->eff_wpm, cfg->tone_hz, cfg->koch_lesson,
             cfg->rotate_180, cw_settings_was_reset());

    /* 网络配置存在独立的 NVS key，与 cw_settings 互不影响——往 cw_settings_t
     * 里加字段会清空用户的 Koch 进度，详见 net_store.h 顶部的说明。
     * 同样不能用 ESP_ERROR_CHECK：NVS 与 fmo-companion 共享，写满时这里会失败，
     * 而 net_store_init() 的每条失败分支都已把结构体填成默认值，固件照常运行。 */
    esp_err_t net_err = net_store_init();
    if (net_err != ESP_OK) {
        ESP_LOGE(TAG, "net_store_init failed: %s - network settings will NOT persist",
                 esp_err_to_name(net_err));
    }
    /* 训练统计同样是独立 key。丢了只是少看几张图，不影响任何功能，
     * 所以失败只记日志。 */
    esp_err_t stats_err = stats_store_init();
    if (stats_err != ESP_OK) {
        ESP_LOGE(TAG, "stats_store_init failed: %s - stats will NOT persist",
                 esp_err_to_name(stats_err));
    }

    /* 音频 */
    if (tone_gen_init() == ESP_OK) {
        tone_gen_set_freq(cfg->tone_hz);
        tone_gen_set_volume(cfg->volume);
    }
    morse_timing_t tm;
    morse_timing_compute(cfg->char_wpm, cfg->eff_wpm, &tm);
    ESP_ERROR_CHECK(cw_player_init());
    cw_player_set_timing(&tm);
    ESP_ERROR_CHECK(key_input_init());

    /* 显示 */
    backlight_init();
    lv_init();
    /* 必须在 lv_port_disp_init() 之前：那里才把 rotated 写进 disp_drv 并注册，
     * 之后再调用这个函数不会有任何效果（见 lv_port_disp.c 里的注释）。
     * 板子没有重力传感器（docs/hardware.md 的外设清单里没有任何 IMU），
     * 所以方向只能来自设置项，不存在"自动跟着翻"这条路。 */
    lv_port_disp_set_rotate_180(cfg->rotate_180 != 0);
    lv_port_disp_init();
    lv_port_indev_init();
#if CW_SCREENSHOT
    /* 必须在 lv_port_disp_init() 之后：钩子是串在已注册的 flush_cb 上的 */
    screenshot_install();
#endif
    const esp_timer_create_args_t tick_args = { .callback = &lv_tick_task, .name = "lv_tick" };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));
    backlight_set(cfg->brightness);

    ui_nav_show(ui_home_create);
#if CW_SCREENSHOT
    /* 放在 battery_start 和 WiFi 之前跑：此刻电量还是初值 100、WiFi 等级 0、
       时钟还没对上（--:--）、OTA 还没查过——页面上会自己变的东西最少，
       指纹的可比性最高。 */
    screenshot_tour();
#endif
    ESP_ERROR_CHECK(battery_start(battery_cb));

    /*
     * 开机提示音：一个 V（...-）。
     *
     * 选 V 是因为它在 CW 里就是**试机信号**——历史上调谐、试设备发的就是
     * VVV，含义正是「设备开着、工作正常」，和开机提示音要表达的东西完全
     * 一致。这台机器是训练器，它发出的每个声音都是教材，不能拿一个语义不对
     * 的信号天天教人：CQ 是「呼叫任意电台寻求通联」，开机时并没有在呼叫谁；
     * 73 是告别、AR/SK 是结束，放在开机更是反的。
     *
     * 速度固定 20 WPM，不跟用户的 char_wpm 走：把速度设成 5 WPM 的人开机要
     * 干等好几秒，而这一下本来就该是「一响」而不是「一段」。
     *
     * 音量走现有的音量设置（tone_gen_set_volume 上面已经设过）——设成静音的
     * 人就该是静音，不需要再多一个开关。
     */
    s_user_timing = tm;
    morse_timing_t chime;
    morse_timing_compute(BOOT_CHIME_WPM, BOOT_CHIME_WPM, &chime);
    cw_player_set_timing(&chime);
    cw_player_play("V", boot_chime_done, NULL);

    /*
     * 开机就把 WiFi 连上并一直保持。
     *
     * 原来是按需开关的，为的是不让射频的电流尖峰窜进内置 DAC 的侧音——但那条
     * 2026-08-28 实测否掉了：联网对练全程听不出可闻底噪，而那正是发射最密集的
     * 场景。代价换回来的是进对练页不用再干等几秒握手。
     *
     * 没存过任何 WiFi 就别开射频：连不上任何东西，纯耗电。
     */
    if (net_store_has_wifi()) {
        ESP_LOGI(TAG, "free heap before wifi: %lu", (unsigned long)esp_get_free_heap_size());
        wifi_net_init();
        wifi_net_start(NULL, NULL);
        /* 主页左上角那个时钟在这之前永远是 --:--，因为系统时间从没被设过。
           放在 wifi_net_start() 之后但不等它连上：SNTP 客户端每次请求才解析
           主机名，网络没通就是失败重试，通了自己会对上。 */
        time_sync_start();
    }

    /* 开机检查一次更新（可在设置里关掉）。已经连着的话 wifi_net_start() 是
       空操作。 */
    if (net_store_get()->boot_check) {
        wifi_net_init();
        wifi_net_start(NULL, NULL);
        ota_check_start_auto(true);
    }
    ESP_LOGI(TAG, "ui ready");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5));
        /* 只锁 lv_timer_handler() 本身：它是本文件里除 lv_init 系初始化和
         * lv_tick_inc（LVGL 文档明确说明可从中断安全调用的计数器自增，不碰
         * 共享结构）之外唯一的 LVGL 调用点，且是与 ui_async_post() 里
         * lv_async_call() 并发的那一侧——两边合起来才是完整的互斥范围，
         * 详见 ui_async.c 的注释。不能把锁的范围扩大到包住上面的 vTaskDelay，
         * 否则整个睡眠期间都会挡住 core 1 电键任务等所有生产者的 ui_async_post()。 */
        ui_lock();
        lv_timer_handler();
        ui_unlock();
    }
}
