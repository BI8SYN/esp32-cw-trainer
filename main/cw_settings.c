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
    c->keyer_mode = 2;
    c->paddle_swap = 0;
    /*
     * 呼号**留空**，不给出厂默认值。
     *
     * 原来这里是 "BI8SYN"。呼号会作为站点 ID 上到 wire 上，而没人会去改一个
     * 「看起来已经填好了」的设置——结果两个人各拿一台新设备约同一条 wire，
     * 双方报出去的 ID 是同一个，顶栏、收报记录里谁是谁全分不清。
     *
     * 留空则是个明确的「还没填」，联网对练进不去（ui_net_create() 会先拦下来
     * 要求填），本地练习照常（trainer 遇到空呼号回退成 NOCALL）。
     */
    c->koch_lesson = 1;
    memset(c->koch_last_pct, 0xFF, sizeof c->koch_last_pct);
    c->brightness = 80;
    c->rotate_180 = 0;
    /* 出厂默认显示图标+数字：绝大多数人是接了电池的，而且这也是 v3 之前
       唯一的行为——老设备升级上来不该看到界面无缘无故变了。 */
    c->battery_show = CW_BATT_NUM;
    /* 提示音默认开：加它的目的是给嫌吵的人一条退路，而不是让绝大多数人
       先去设置里找开关才有反馈。 */
    c->sound_on = 1;
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
/* v2 的结构体布局快照，仅用于升级迁移，不要再改动它。
 *
 * 与 v1、v3 一样都是 68 字节——v3 删掉了 key_input、又在末尾加了 battery_show，
 * 一减一加恰好抵消。三个版本长度全相同，所以 cw_settings_init() 里**必须靠
 * version 字段分辨**，只看 len 是分不出来的。 */
typedef struct {
    uint32_t version;
    uint8_t  char_wpm;
    uint8_t  eff_wpm;
    uint16_t tone_hz;
    uint8_t  volume;
    uint8_t  key_input;    /* v3 删除：电键输入二选一，两路改并联后没有意义了 */
    uint8_t  keyer_mode;
    uint8_t  paddle_swap;
    char     callsign[12];
    uint8_t  koch_lesson;
    uint8_t  koch_last_pct[41];
    uint8_t  brightness;
    uint8_t  rotate_180;
} cw_settings_v2_t;

/* 历史布局是冻结的：它们描述的是**已经写在别人设备 NVS 里的字节**，改一个字节
   就会把老配置读错位。谁要是"顺手整理"了上面两个快照，在这里当场编译失败，
   而不是等到用户的 Koch 进度悄悄错乱。
   断言消息用 ASCII：tools/check_charset.py 会把字符串字面量算进"界面用到的字"，
   中文消息会白白占掉裁剪字库的位置，而它根本不会上屏。 */
/* v3 的结构体布局快照，仅用于升级迁移，不要再改动它。
 * v4 在末尾加了 sound_on，结构体从 68 涨到 72——v4 是第一个长度和前面不同的
 * 版本。即便如此，分派仍然按 version 字段走，长度只作为一道额外的防线。 */
typedef struct {
    uint32_t version;
    uint8_t  char_wpm;
    uint8_t  eff_wpm;
    uint16_t tone_hz;
    uint8_t  volume;
    uint8_t  keyer_mode;
    uint8_t  paddle_swap;
    char     callsign[12];
    uint8_t  koch_lesson;
    uint8_t  koch_last_pct[41];
    uint8_t  brightness;
    uint8_t  rotate_180;
    uint8_t  battery_show;
} cw_settings_v3_t;

_Static_assert(sizeof(cw_settings_v1_t) == 68, "v1 layout is frozen");
_Static_assert(sizeof(cw_settings_v2_t) == 68, "v2 layout is frozen");
_Static_assert(sizeof(cw_settings_v3_t) == 68, "v3 layout is frozen");

static void migrate_v3(const cw_settings_v3_t *old, cw_settings_t *dst)
{
    set_defaults(dst);
    dst->version      = CW_SETTINGS_VERSION;
    dst->char_wpm     = old->char_wpm;
    dst->eff_wpm      = old->eff_wpm;
    dst->tone_hz      = old->tone_hz;
    dst->volume       = old->volume;
    dst->keyer_mode   = old->keyer_mode;
    dst->paddle_swap  = old->paddle_swap;
    memcpy(dst->callsign, old->callsign, sizeof dst->callsign);
    dst->callsign[sizeof dst->callsign - 1] = 0;
    dst->koch_lesson  = old->koch_lesson;
    memcpy(dst->koch_last_pct, old->koch_last_pct, sizeof dst->koch_last_pct);
    dst->brightness   = old->brightness;
    dst->rotate_180   = old->rotate_180;
    dst->battery_show = old->battery_show;
    /* sound_on 保持 set_defaults() 给的 1：老设备升上来提示音是开的 */
}

static void migrate_v2(const cw_settings_v2_t *old, cw_settings_t *dst)
{
    set_defaults(dst);
    dst->version     = CW_SETTINGS_VERSION;
    dst->char_wpm    = old->char_wpm;
    dst->eff_wpm     = old->eff_wpm;
    dst->tone_hz     = old->tone_hz;
    dst->volume      = old->volume;
    /* key_input 不搬：v3 里这个字段不存在了 */
    dst->keyer_mode  = old->keyer_mode;
    dst->paddle_swap = old->paddle_swap;
    memcpy(dst->callsign, old->callsign, sizeof dst->callsign);
    dst->callsign[sizeof dst->callsign - 1] = 0;
    dst->koch_lesson = old->koch_lesson;
    memcpy(dst->koch_last_pct, old->koch_last_pct, sizeof dst->koch_last_pct);
    dst->brightness  = old->brightness;
    dst->rotate_180  = old->rotate_180;
    /* battery_show 保持 set_defaults() 给的 CW_BATT_NUM：老设备升上来行为不变 */
}

static void migrate_v1(const cw_settings_v1_t *old, cw_settings_t *dst)
{
    set_defaults(dst);
    dst->version     = CW_SETTINGS_VERSION;
    dst->char_wpm    = old->char_wpm;
    dst->eff_wpm     = old->eff_wpm;
    dst->tone_hz     = old->tone_hz;
    dst->volume      = old->volume;
    /* key_input 不搬：v3 里这个字段不存在了 */
    dst->keyer_mode  = old->keyer_mode;
    dst->paddle_swap = old->paddle_swap;
    memcpy(dst->callsign, old->callsign, sizeof dst->callsign);
    dst->callsign[sizeof dst->callsign - 1] = 0;
    dst->koch_lesson = old->koch_lesson;
    memcpy(dst->koch_last_pct, old->koch_last_pct, sizeof dst->koch_last_pct);
    dst->brightness  = old->brightness;
    /* rotate_180 / battery_show 保持 set_defaults() 给的值：
       老设备升级上来默认不翻转、电量照常显示 */
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
    /* v1/v2/v3 的 sizeof 恰好都是 68，v4 是 72。历史上只能靠 version 字段
       分辨，现在长度也能帮上忙，但判据仍然是 version——别依赖长度。 */
    union {
        cw_settings_t    cur;
        cw_settings_v3_t v3;
        cw_settings_v2_t v2;
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
    if (err == ESP_OK && len == sizeof(cw_settings_v2_t) && raw.v2.version == 2) {
        ESP_LOGI(TAG, "migrating config v2 -> v%d", CW_SETTINGS_VERSION);
        migrate_v2(&raw.v2, &s_cfg);
        return write_blob(&s_cfg);
    }
    if (err == ESP_OK && len == sizeof(cw_settings_v3_t) && raw.v3.version == 3) {
        ESP_LOGI(TAG, "migrating config v3 -> v%d", CW_SETTINGS_VERSION);
        migrate_v3(&raw.v3, &s_cfg);
        return write_blob(&s_cfg);
    }
    if (err == ESP_OK && len == sizeof s_cfg) {
        memcpy(&s_cfg, &raw.cur, sizeof s_cfg);
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
