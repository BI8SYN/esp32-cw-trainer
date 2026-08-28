#include "wifi_net.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
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
static int              s_rssi;
/* -1 = 自动选最强的；>=0 = 用户在设置里指定的槽位 */
static int              s_prefer = -1;
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
    /*
     * 明确设省电模式，不吃 IDF 的默认值——射频现在是常连的，空闲电流直接决定
     * 续航，这个值不能随某次 IDF 升级悄悄变掉。MIN_MODEM 在 DTIM 间隙关收发，
     * 对我们这种「几秒一个小包」的流量几乎没有延迟代价。
     */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
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

/*
 * 在扫描结果里找信号最强的已保存槽位。返回槽位下标，-1 表示一个都没扫到。
 *
 * tried 是已经试过并失败的槽位位图。原来只挑最强的一个、连不上就放弃——
 * 而「最强的那个密码是错的、旁边还有一个能连」是很常见的情形（家里路由改了
 * 密码、隔壁的同名 AP），放弃等于明明有网却上不去。现在逐个往下试。
 */
static int pick_profile(const wifi_ap_record_t *recs, uint16_t n, uint32_t tried)
{
    const cw_net_cfg_t *cfg = net_store_get();
    int best = -1, best_rssi = -128;

    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        if (tried & (1u << i)) continue;
        if (!cfg->profiles[i].used || !cfg->profiles[i].ssid[0]) continue;
        /* 用户在设置里指定了槽位就只认它——"信号最强"未必是想用的那个。 */
        if (s_prefer >= 0 && i != s_prefer) continue;
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
    s_rssi = best >= 0 ? best_rssi : 0;
    return best;
}

/* 连一个具体槽位，等到拿到 IP 或失败。连上返回 true。 */
static bool connect_slot(int idx)
{
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
            ESP_LOGI(TAG, "connected: %s (free heap %lu)", cfg->profiles[idx].ssid,
                     (unsigned long)esp_get_free_heap_size());
            report(WIFI_NET_CONNECTED, cfg->profiles[idx].ssid);
            return true;
        }
        if (s_disconnected) {
            /* 扫到了这个 SSID 却连不上，绝大多数情况是密码错。与「扫不到」
               区分开报，因为用户要做的事完全不同：一个是改密码，一个是换网络。 */
            ESP_LOGW(TAG, "auth/assoc failed for %s", cfg->profiles[idx].ssid);
            report(WIFI_NET_FAIL_AUTH, cfg->profiles[idx].ssid);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGW(TAG, "connect timeout for %s", cfg->profiles[idx].ssid);
    report(WIFI_NET_FAIL_AUTH, cfg->profiles[idx].ssid);
    esp_wifi_disconnect();      /* 超时也要收手，否则下一轮 connect 会撞上它 */
    return false;
}

/* 扫描一次，按信号从强到弱把扫到的已保存槽位逐个试过去。连上返回 true。 */
static bool try_connect(void)
{
    wifi_lock();
    if (radio_on() != ESP_OK) {
        wifi_unlock();
        report(WIFI_NET_FAIL_NO_AP, NULL);
        return false;
    }
    report(WIFI_NET_SCANNING, NULL);
    wifi_ap_record_t *recs = NULL;
    uint16_t n = scan_once(&recs);
    wifi_unlock();

    uint32_t tried = 0;
    bool any = false;
    while (recs && n) {
        int idx = pick_profile(recs, n, tried);
        if (idx < 0) break;
        any = true;
        tried |= 1u << idx;
        if (connect_slot(idx)) { free(recs); return true; }
    }
    free(recs);

    if (!any) {
        ESP_LOGW(TAG, "no saved network in range (%u AP scanned)", n);
        report(WIFI_NET_FAIL_NO_AP, NULL);
    }
    return false;
}

/* 退避：首次 5 秒，逐次加倍，封顶 60 秒。 */
#define RETRY_MIN_MS  5000
#define RETRY_MAX_MS  60000

/*
 * 常驻任务：连上就守着，掉线就重连，连不上就退避重试。
 *
 * 射频不再关闭，所以这个任务起来之后就不退出了——除非一个已保存的网络都没有，
 * 那种情况下重试是空转，退出等配网页存好了再由它拉起来。
 */
static void worker(void *arg)
{
    (void)arg;
    uint32_t backoff = RETRY_MIN_MS;

    for (;;) {
        if (try_connect()) {
            backoff = RETRY_MIN_MS;
            /* 守着。「保持连接」不是「连上一次」——AP 重启、走出覆盖范围、
               信道拥塞都会把链路打断，而 ESP-IDF 不会自己重连，得我们来。
               切换「连接网络」也走这条路：wifi_net_prefer() 主动断开，
               下一轮重连时 pick_profile() 就认新的槽位了。 */
            while (!s_disconnected) vTaskDelay(pdMS_TO_TICKS(500));
            ESP_LOGW(TAG, "link lost, reconnecting");
            report(WIFI_NET_CONNECTING, NULL);
            continue;
        }

        /* 一个网络都没存，重试没有意义。 */
        if (!net_store_has_wifi()) break;

        for (uint32_t w = 0; w < backoff; w += 200) vTaskDelay(pdMS_TO_TICKS(200));
        if (backoff < RETRY_MAX_MS) backoff *= 2;
        if (backoff > RETRY_MAX_MS) backoff = RETRY_MAX_MS;
    }

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
    /*
     * 已经连上就什么都不做。射频改成常连之后，各处（对练页、升级页、配网页）
     * 都会「先 start 一下再用」，不挡住的话每一次都会重新起一个 worker 去扫描
     * 重连——把一条好端端的连接掐掉几秒，纯属自找。
     */
    if (s_got_ip) return ESP_OK;

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

bool             wifi_net_is_connected(void) { return s_got_ip; }

/* RSSI 分三格。-60 以上算满格，-75 以下算一格——这两条线是 2.4G 常用的
   经验值，不必精确，够区分"能用/勉强/快断了"即可。 */
int wifi_net_level(void)
{
    if (!s_got_ip) return -1;
    if (s_rssi >= -60) return 3;
    if (s_rssi >= -75) return 2;
    return 1;
}

void wifi_net_prefer(int slot)
{
    if (slot == s_prefer) return;
    s_prefer = slot;

    /*
     * 主动断一下，让 worker 的重连那一轮带着新槽位重新挑。
     *
     * 射频改成常连之后，这一步是必需的：原来只要改掉 s_prefer 就行，因为下一次
     * 连接总会到来（每次进对练页都会重连一遍）。现在连接是一直在的，「下一次」
     * 不会自己来——不断开的话，用户在设置里换了网络却毫无反应。
     */
    if (s_got_ip) {
        ESP_LOGI(TAG, "preferred network changed, reconnecting");
        esp_wifi_disconnect();
    }
}
int  wifi_net_preferred(void) { return s_prefer; }
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
