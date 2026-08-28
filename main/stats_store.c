#include "stats_store.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "stats_store";

/* 与 cw_settings / net_store 同一个命名空间，各自独立的 key 和版本号。 */
#define NVS_NS   "cwtrn"
#define NVS_KEY  "stats"

static cw_stats_t s_stats;

static esp_err_t write_blob(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY, &s_stats, sizeof s_stats);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t stats_store_init(void)
{
    cw_stats_init(&s_stats);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s - stats will not persist", esp_err_to_name(err));
        return err;
    }
    size_t len = sizeof s_stats;
    err = nvs_get_blob(h, NVS_KEY, &s_stats, &len);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        cw_stats_init(&s_stats);
        return write_blob();
    }
    if (err != ESP_OK || len != sizeof s_stats || s_stats.version != CW_STATS_VERSION) {
        /* 只重置统计。cw_settings 那边一个字节都不碰——统计丢了无所谓，
           Koch 进度丢了是事故。 */
        ESP_LOGW(TAG, "stats invalid (err=%s len=%u ver=%lu), reset",
                 esp_err_to_name(err), (unsigned)len, (unsigned long)s_stats.version);
        cw_stats_init(&s_stats);
        return write_blob();
    }
    ESP_LOGI(TAG, "stats loaded: %u 条历史", s_stats.hist_n);
    return ESP_OK;
}

cw_stats_t *stats_store_get(void) { return &s_stats; }

esp_err_t stats_store_save(void) { return write_blob(); }

esp_err_t stats_store_reset(void)
{
    cw_stats_init(&s_stats);
    return write_blob();
}

void stats_store_record(const trainer_t *t)
{
    if (!t) return;

    /* 把 errors[] 里的期望字符拼成一个串交给 cw_stats_add——它要的是
       "哪些字母被抄错了"，用户答成什么无关紧要。 */
    char wrong[TRAINER_MAX_ERRORS + 1];
    int n = 0;
    for (int i = 0; i < t->error_count && n < TRAINER_MAX_ERRORS; i++) {
        char e = t->errors[i].expected;
        if (e && e != '_') wrong[n++] = e;
    }
    wrong[n] = '\0';

    cw_stats_add(&s_stats, t->lesson, trainer_percent(t), t->shown, wrong);
    stats_store_save();
}
