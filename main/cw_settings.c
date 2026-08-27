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
    c->rotate_180 = 0;
}

/* v1 的结构体布局快照，仅用于升级迁移，不要再改动它。
 *
 * 注意 v1 -> v2 只是在末尾追加了一个 uint8_t，落在原来的尾部填充字节里，
 * 所以两者的 sizeof 恰好相同（都是 68 字节）——只靠 blob 长度分辨不出版本，
 * 必须看 version 字段。这也意味着如果不做迁移，老配置会因为 version 不匹配
 * 被整个重置，Koch 进度（koch_last_pct）会跟着一起丢。 */
typedef struct {
    uint32_t version;
    uint8_t  char_wpm;
    uint8_t  eff_wpm;
    uint16_t tone_hz;
    uint8_t  volume;
    uint8_t  key_input;
    uint8_t  keyer_mode;
    uint8_t  paddle_swap;
    char     callsign[12];
    uint8_t  koch_lesson;
    uint8_t  koch_last_pct[41];
    uint8_t  brightness;
} cw_settings_v1_t;

/* 把 v1 的每个字段显式搬到 v2，新字段取默认值。逐字段拷贝而不是 memcpy 整块：
 * 万一以后 v2 在中间插入字段，这里会立刻编译报错或写错位置被看出来，
 * 而 memcpy 会静默地把所有后续字段错位。 */
static void migrate_v1(const cw_settings_v1_t *old, cw_settings_t *dst)
{
    set_defaults(dst);
    dst->version     = CW_SETTINGS_VERSION;
    dst->char_wpm    = old->char_wpm;
    dst->eff_wpm     = old->eff_wpm;
    dst->tone_hz     = old->tone_hz;
    dst->volume      = old->volume;
    dst->key_input   = old->key_input;
    dst->keyer_mode  = old->keyer_mode;
    dst->paddle_swap = old->paddle_swap;
    memcpy(dst->callsign, old->callsign, sizeof dst->callsign);
    dst->callsign[sizeof dst->callsign - 1] = 0;
    dst->koch_lesson = old->koch_lesson;
    memcpy(dst->koch_last_pct, old->koch_last_pct, sizeof dst->koch_last_pct);
    dst->brightness  = old->brightness;
    /* rotate_180 保持 set_defaults() 给的 0：老设备升级上来默认不翻转 */
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
    union {
        cw_settings_t    v2;
        cw_settings_v1_t v1;
    } raw;
    size_t len = sizeof raw;
    err = nvs_get_blob(h, NVS_KEY, &raw, &len);
    nvs_close(h);

    if (err == ESP_OK && len == sizeof(cw_settings_v1_t) && raw.v1.version == 1) {
        ESP_LOGI(TAG, "migrating config v1 -> v%d", CW_SETTINGS_VERSION);
        migrate_v1(&raw.v1, &s_cfg);
        return write_blob(&s_cfg);
    }
    if (err == ESP_OK && len == sizeof s_cfg) {
        memcpy(&s_cfg, &raw.v2, sizeof s_cfg);
    }

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
