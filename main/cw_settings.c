#include "cw_settings.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "cw_settings";
#define NVS_NS  "cwtrn"
#define NVS_KEY "cfg"

static cw_settings_t s_cfg;
static bool s_was_reset = false;

static void set_defaults(cw_settings_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = CW_SETTINGS_VERSION;
    c->char_wpm = 20;
    c->eff_wpm = 15;
    c->tone_hz = 600;
    c->volume = 70;
    c->key_input = CW_KEY_INPUT_TOUCH;
    c->keyer_mode = 2;
    c->paddle_swap = 0;
    strcpy(c->callsign, "BI8SYN");
    c->koch_lesson = 1;
    memset(c->koch_last_pct, 0xFF, sizeof c->koch_last_pct);
    c->brightness = 80;
}

static esp_err_t write_blob(const cw_settings_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY, c, sizeof *c);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t cw_settings_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        set_defaults(&s_cfg);
        return err;
    }
    size_t len = sizeof s_cfg;
    err = nvs_get_blob(h, NVS_KEY, &s_cfg, &len);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no config, first run");
        set_defaults(&s_cfg);
        return write_blob(&s_cfg);
    }
    if (err != ESP_OK || len != sizeof s_cfg || s_cfg.version != CW_SETTINGS_VERSION) {
        ESP_LOGW(TAG, "config invalid (err=%s len=%u ver=%lu), reset",
                 esp_err_to_name(err), (unsigned)len, (unsigned long)s_cfg.version);
        set_defaults(&s_cfg);
        s_was_reset = true;
        return write_blob(&s_cfg);
    }
    return ESP_OK;
}

cw_settings_t *cw_settings_get(void) { return &s_cfg; }

esp_err_t cw_settings_save(void)
{
    esp_err_t err = write_blob(&s_cfg);
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t cw_settings_reset(void)
{
    set_defaults(&s_cfg);
    return write_blob(&s_cfg);
}

bool cw_settings_was_reset(void) { return s_was_reset; }
