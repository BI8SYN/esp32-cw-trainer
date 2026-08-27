#include "wifi_net.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "net_store.h"

static const char *TAG = "wifi_net";

#define SSID_BYTES        32           /* 802.11 SSID 最长 32 字节，不含 NUL */
#define CONNECT_TIMEOUT_MS 15000

static wifi_net_state_t s_state = WIFI_NET_OFF;
static char             s_ssid[33];
static wifi_net_cb_t    s_cb;
static void            *s_user;
static bool             s_netif_ready;
static bool             s_radio_on;
static bool             s_got_ip;
static bool             s_disconnected;
static TaskHandle_t     s_task;
static esp_netif_t     *s_netif;
/*
 * 串行化所有 esp_wifi_* 操作序列。配网页会同时存在两个使用者：
 * wifi_net_start() 拉起的连接任务，和 ui_wifi 的扫描任务。两者都要
 * radio_on() 和 esp_wifi_scan_start()，并发时后者直接返回
 * ESP_ERR_WIFI_STATE——表现是「有时候扫不出任何网络」这种偶发故障。
 */
static SemaphoreHandle_t s_mux;

static void wifi_lock(void)   { if (s_mux) xSemaphoreTake(s_mux, portMAX_DELAY); }
static void wifi_unlock(void) { if (s_mux) xSemaphoreGive(s_mux); }

static void report(wifi_net_state_t st, const char *ssid)
{
    s_state = st;
    if (ssid) snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    else if (st != WIFI_NET_CONNECTED) s_ssid[0] = '\0';
    if (s_cb) s_cb(st, s_ssid, s_user);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_disconnected = true;
        s_got_ip = false;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
        s_disconnected = false;
    }
}

esp_err_t wifi_net_init(void)
{
    if (!s_mux) s_mux = xSemaphoreCreateMutex();
    if (s_netif_ready) return ESP_OK;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_FAIL;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    s_netif_ready = true;
    return ESP_OK;
}

/* 开射频。分配 WiFi 驱动的那 ~50KB 就发生在 esp_wifi_init() 这一步，
   所以它属于 start()，不属于 init()。 */
static esp_err_t radio_on(void)
{
    if (s_radio_on) return ESP_OK;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) return err;
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    s_radio_on = true;
    return ESP_OK;
}

/* 扫描一次，把结果写进 records（调用方负责 free）。返回条数，0 表示失败或无 AP。 */
static uint16_t scan_once(wifi_ap_record_t **records)
{
    wifi_scan_config_t sc = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 50,
        .scan_time.active.max = 120,
    };
    esp_err_t err = esp_wifi_scan_start(&sc, true);   /* 阻塞式 */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t n = 0;
    if (esp_wifi_scan_get_ap_num(&n) != ESP_OK || n == 0) return 0;

    wifi_ap_record_t *r = calloc(n, sizeof *r);
    if (!r) {
        ESP_LOGE(TAG, "alloc %u scan records failed", n);
        return 0;
    }
    if (esp_wifi_scan_get_ap_records(&n, r) != ESP_OK) {
        free(r);
        return 0;
    }
    *records = r;
    return n;
}

static void ssid_of(const wifi_ap_record_t *r, char out[33])
{
    memset(out, 0, 33);
    memcpy(out, r->ssid, SSID_BYTES);
    out[SSID_BYTES] = '\0';
}

/* 在扫描结果里找信号最强的已保存槽位。返回槽位下标，-1 表示一个都没扫到。 */
static int pick_profile(const wifi_ap_record_t *recs, uint16_t n)
{
    const cw_net_cfg_t *cfg = net_store_get();
    int best = -1, best_rssi = -128;

    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        if (!cfg->profiles[i].used || !cfg->profiles[i].ssid[0]) continue;
        for (uint16_t j = 0; j < n; j++) {
            char s[33];
            ssid_of(&recs[j], s);
            if (strcmp(s, cfg->profiles[i].ssid) != 0) continue;
            if ((int)recs[j].rssi > best_rssi) {
                best_rssi = recs[j].rssi;
                best = i;
            }
        }
    }
    return best;
}

static void worker(void *arg)
{
    (void)arg;

    wifi_lock();
    if (radio_on() != ESP_OK) {
        wifi_unlock();
        report(WIFI_NET_FAIL_NO_AP, NULL);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    report(WIFI_NET_SCANNING, NULL);

    wifi_ap_record_t *recs = NULL;
    uint16_t n = scan_once(&recs);
    int idx = (n && recs) ? pick_profile(recs, n) : -1;
    free(recs);
    wifi_unlock();

    if (idx < 0) {
        ESP_LOGW(TAG, "no saved network in range (%u AP scanned)", n);
        report(WIFI_NET_FAIL_NO_AP, NULL);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    const cw_net_cfg_t *cfg = net_store_get();
    report(WIFI_NET_CONNECTING, cfg->profiles[idx].ssid);

    wifi_config_t wc = { 0 };
    /* wifi_config_t 里的 ssid 是 32 字节且**不要求** NUL 结尾（满 32 字节时
       就是没有 NUL 的），而我们存的是 char[33]。用 snprintf 会被 GCC 判成
       可能截断（-Werror=format-truncation），而且语义上也不对——这里要的
       就是"最多拷 32 字节，不补 NUL"。 */
    size_t sl = strnlen(cfg->profiles[idx].ssid, SSID_BYTES);
    memcpy(wc.sta.ssid, cfg->profiles[idx].ssid, sl);
    size_t pl = strnlen(cfg->profiles[idx].password, sizeof wc.sta.password);
    memcpy(wc.sta.password, cfg->profiles[idx].password, pl);

    s_got_ip = false;
    s_disconnected = false;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();

    /* 等到拿到 IP 为止。只看 STA_CONNECTED 是不够的——没有 IP 就没法开 socket。 */
    for (int waited = 0; waited < CONNECT_TIMEOUT_MS; waited += 100) {
        if (s_got_ip) {
            ESP_LOGI(TAG, "connected: %s", cfg->profiles[idx].ssid);
            report(WIFI_NET_CONNECTED, cfg->profiles[idx].ssid);
            s_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        if (s_disconnected) {
            /* 扫到了这个 SSID 却连不上，绝大多数情况是密码错。与「扫不到」
               区分开报，因为用户要做的事完全不同：一个是改密码，一个是换网络。 */
            ESP_LOGW(TAG, "auth/assoc failed for %s", cfg->profiles[idx].ssid);
            report(WIFI_NET_FAIL_AUTH, cfg->profiles[idx].ssid);
            s_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "connect timeout for %s", cfg->profiles[idx].ssid);
    report(WIFI_NET_FAIL_AUTH, cfg->profiles[idx].ssid);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t wifi_net_start(wifi_net_cb_t cb, void *user)
{
    if (!s_netif_ready) {
        esp_err_t err = wifi_net_init();
        if (err != ESP_OK) return err;
    }
    if (s_task) return ESP_ERR_INVALID_STATE;   /* 已经在连了 */

    s_cb = cb;
    s_user = user;

    /* 钉在 core 0：WiFi 与 lwIP 都在这一核，而 core 1 上跑着优先级 23 的
       tone 任务和 22 的 key_input 任务，绝不能去抢它们。 */
    if (xTaskCreatePinnedToCore(worker, "wifi_net", 4096, NULL, 5, &s_task, 0) != pdPASS) {
        s_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void wifi_net_stop(void)
{
    if (!s_radio_on) {
        report(WIFI_NET_OFF, NULL);
        return;
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
    /* deinit 才真正把驱动的那几十 KB 还回堆里；只 stop() 是留着的。 */
    esp_wifi_deinit();
    s_radio_on = false;
    s_got_ip = false;
    report(WIFI_NET_OFF, NULL);
}

bool             wifi_net_is_connected(void) { return s_got_ip; }
wifi_net_state_t wifi_net_state(void)        { return s_state; }
const char      *wifi_net_ssid(void)         { return s_ssid; }

int wifi_net_scan(char out[][33], int max_n)
{
    wifi_lock();
    if (!s_radio_on && radio_on() != ESP_OK) { wifi_unlock(); return 0; }

    wifi_ap_record_t *recs = NULL;
    uint16_t n = scan_once(&recs);
    wifi_unlock();
    if (!n || !recs) return 0;

    int cnt = 0;
    for (uint16_t i = 0; i < n && cnt < max_n; i++) {
        char s[33];
        ssid_of(&recs[i], s);
        if (!s[0]) continue;                     /* 隐藏 SSID */
        bool dup = false;
        for (int j = 0; j < cnt; j++) {
            if (strcmp(out[j], s) == 0) { dup = true; break; }
        }
        if (dup) continue;                       /* 同一 SSID 的多个 AP 只列一次 */
        snprintf(out[cnt], 33, "%s", s);
        cnt++;
    }
    free(recs);
    return cnt;
}
