#include "time_sync.h"
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "time_sync";

/*
 * 只配一台服务器：CONFIG_LWIP_SNTP_MAX_SERVERS 是 1，要多配就得改 sdkconfig
 * 并重新生成——而 `ntp.aliyun.com` 的 DNS 本来就轮转多个 IP，lwip 又是每次
 * 请求现解析主机名，冗余已经在 DNS 那一层了。
 *
 * 选阿里云而不是 `pool.ntp.org`：整套后端（wire 服务器、固件清单）都在阿里云
 * 广州，而 README 记着国际线路实测丢包 17~24%。对时是 UDP 单包一问一答，
 * 丢了就是又等一个周期。
 */
#define NTP_SERVER "ntp.aliyun.com"

/* POSIX 的 TZ 串符号是反的：UTC+8 写成 -8。 */
#define TZ_CHINA   "CST-8"

static bool s_started;
static bool s_synced;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "time synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static bool s_running;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    if (!s_running) {
        esp_netif_sntp_start();
        s_running = true;
    } else {
        /* 已经在跑了：重连之后催它立刻发一次，不用等下一个更新周期。 */
        esp_sntp_restart();
    }
}

void time_sync_start(void)
{
    if (s_started) return;

    /* 先设时区再启动：对时回调里就要按本地时间打日志了。 */
    setenv("TZ", TZ_CHINA, 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    /*
     * 不在这里就开始请求。本函数是开机时调的，那一刻 WiFi 还在扫描/握手，
     * 第一个请求必然打空，之后要等 lwip 的指数退避——实测第一次对上要 81 秒，
     * 用户盯着 `--:--` 一分多钟。改成拿到 IP 再开，第一发就中。
     */
    cfg.start = false;
    cfg.sync_cb = on_sync;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        /* 对不上时只是时钟显示 `--:--`，别的功能一概不受影响，所以记日志
           继续跑，不往上报错、更不 abort。 */
        ESP_LOGW(TAG, "sntp init failed: %s - clock stays --:--", esp_err_to_name(err));
        return;
    }
    /*
     * 每次拿到 IP 都催一次。除了开机第一次，重连之后也该立刻校一遍——设备
     * 可能在没网的时候躺了很久，而 SNTP 自己的下一次更新要等一小时
     * （CONFIG_LWIP_SNTP_UPDATE_DELAY）。
     */
    esp_err_t reg = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL, NULL);
    if (reg != ESP_OK) {
        /* 注册不上就退回「自己慢慢退避」，仍然能对上，只是慢。 */
        ESP_LOGW(TAG, "ip event register failed: %s - falling back to sntp retry",
                 esp_err_to_name(reg));
        esp_netif_sntp_start();
    }

    s_started = true;
    ESP_LOGI(TAG, "sntp started (%s, %s)", NTP_SERVER, TZ_CHINA);
}

bool time_sync_ok(void) { return s_synced; }
