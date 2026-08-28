#include "ota.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "wifi_net.h"

static const char *TAG = "ota";

#define BUF_SZ 2048

static ota_status_t s_st;
static char         s_url[160];
static TaskHandle_t s_task;

static void set(ota_state_t st, int pct, const char *msg)
{
    s_st.state = st;
    s_st.percent = pct;
    snprintf(s_st.msg, sizeof s_st.msg, "%s", msg ? msg : "");
    if (st == OTA_FAIL) ESP_LOGE(TAG, "%s", s_st.msg);
    else ESP_LOGI(TAG, "%s (%d%%)", s_st.msg, pct);
}

static void task(void *arg)
{
    (void)arg;

    set(OTA_CONNECTING, 0, "等待 WiFi");
    for (int i = 0; i < 300 && !wifi_net_is_connected(); i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (!wifi_net_is_connected()) {
        set(OTA_FAIL, 0, "WiFi 未连接");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t cfg = {
        .url = s_url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        set(OTA_FAIL, 0, "无法创建 HTTP 客户端");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        set(OTA_FAIL, 0, "连接服务器失败");
        goto done;
    }
    int total = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        char m[48];
        snprintf(m, sizeof m, "服务器返回 %d", status);
        set(OTA_FAIL, 0, m);
        goto done;
    }

    /* 写另一个槽。esp_ota_get_next_update_partition 会自动选当前没在跑的那个。 */
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        set(OTA_FAIL, 0, "找不到可写的分区");
        goto done;
    }
    if (total > 0 && total > (int)part->size) {
        set(OTA_FAIL, 0, "固件大于分区容量");
        goto done;
    }

    esp_ota_handle_t h = 0;
    err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &h);
    if (err != ESP_OK) {
        set(OTA_FAIL, 0, "擦除分区失败");
        goto done;
    }

    set(OTA_DOWNLOADING, total > 0 ? 0 : -1, "下载中");

    char *buf = malloc(BUF_SZ);
    if (!buf) {
        esp_ota_abort(h);
        set(OTA_FAIL, 0, "内存不足");
        goto done;
    }

    int got = 0;
    while (true) {
        int n = esp_http_client_read(cli, buf, BUF_SZ);
        if (n < 0) {
            free(buf);
            esp_ota_abort(h);
            set(OTA_FAIL, 0, "下载中断");
            goto done;
        }
        if (n == 0) break;                 /* 读完 */
        err = esp_ota_write(h, buf, n);
        if (err != ESP_OK) {
            free(buf);
            esp_ota_abort(h);
            set(OTA_FAIL, 0, "写入失败");
            goto done;
        }
        got += n;
        if (total > 0) set(OTA_DOWNLOADING, got * 100 / total, "下载中");
    }
    free(buf);

    if (got == 0) {
        esp_ota_abort(h);
        set(OTA_FAIL, 0, "固件为空");
        goto done;
    }

    /* esp_ota_end 会校验镜像头和校验和；不通过就不会切槽。 */
    err = esp_ota_end(h);
    if (err != ESP_OK) {
        set(OTA_FAIL, 0, err == ESP_ERR_OTA_VALIDATE_FAILED ? "固件校验不通过" : "收尾失败");
        goto done;
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        set(OTA_FAIL, 0, "切换启动分区失败");
        goto done;
    }
    set(OTA_DONE, 100, "完成，可以重启");

done:
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_start(const char *url)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    if (!url || !url[0]) return ESP_ERR_INVALID_ARG;
    snprintf(s_url, sizeof s_url, "%s", url);
    memset(&s_st, 0, sizeof s_st);

    /* 钉在 core 0：和 WiFi/lwIP 同核。优先级 5，远低于 core 1 上的
       tone(23) 与 key_input(22)——升级期间侧音也不能卡。 */
    if (xTaskCreatePinnedToCore(task, "ota", 8192, NULL, 5, &s_task, 0) != pdPASS) {
        s_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

const ota_status_t *ota_status(void) { return &s_st; }

void ota_reboot(void) { esp_restart(); }

void ota_mark_valid(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK) return;
    if (st == ESP_OTA_IMG_PENDING_VERIFY) {
        /* 新固件第一次启动。走到这里说明已经跑到 app_main 了，标记为可用，
           否则 bootloader 会在下次复位时回滚到旧槽。 */
        ESP_LOGI(TAG, "首次启动新固件，标记为可用");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

const char *ota_running_label(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run ? run->label : "?";
}
