#include "net_store.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "cw_settings.h"

static const char *TAG = "net_store";

/* 与 cw_settings 同一个命名空间，但 key 不同——两个 blob 各自带版本号，
   互不影响。见 net_store.h 顶部关于「不能并进 cw_settings_t」的说明。 */
#define NVS_NS           "cwtrn"
#define NVS_KEY_NET_CFG  "net_cfg"

/*
 * fmo-companion 的 WiFi blob 布局。这些偏移是照着
 * firmware/fmo-companion/main/app_settings.c 的 app_wifi_profiles_blob_t
 * 抄下来的：
 *
 *     uint32_t version;                 @0
 *     uint8_t  active_index;            @4
 *     uint8_t  reserved[3];             @5
 *     app_wifi_profile_t profiles[3];   @8   每个 98 字节
 *         uint8_t used;                 +0
 *         char    ssid[33];             +1
 *         char    password[64];         +34
 *
 * 逐字节按偏移解析，**不要**定义一个结构体去 memcpy：两个工程的结构体定义
 * 不同（对方 3 槽、还有 active_index 和 reserved），一旦哪天对方改了布局，
 * memcpy 会静默读出垃圾密码，而按偏移解析至少还有 size/version 两道校验。
 */
#define FMO_NS                 "appcfg"
#define FMO_KEY                "wifi_profiles"
#define FMO_VERSION            1
#define FMO_PROFILE_MAX        3
#define FMO_PROFILE_SIZE       98
#define FMO_PROFILES_OFF       8
#define FMO_BLOB_SIZE          (FMO_PROFILES_OFF + FMO_PROFILE_MAX * FMO_PROFILE_SIZE)  /* 302 */
#define FMO_PROF_USED_OFF      0
#define FMO_PROF_SSID_OFF      1
#define FMO_PROF_SSID_LEN      33
#define FMO_PROF_PASS_OFF      34
#define FMO_PROF_PASS_LEN      64

static cw_net_cfg_t s_cfg;

static void set_defaults(cw_net_cfg_t *c);

/*
 * v1 布局：和 v2 一样，只是末尾没有 ota_url。
 *
 * 加 ota_url 时我直接把版本号从 1 提到 2 而没写迁移，结果整个 blob 回默认
 * ——连同用户存的 WiFi 账号密码一起没了。在电阻屏上重敲一遍 WPA2 密码的
 * 代价很实在，而这正是 cw_settings 那边专门写 migrate_v1() 要避免的事。
 *
 * 逐字段搬而不是 memcpy 整块：万一以后 v3 在中间插字段，逐字段会立刻编译
 * 报错或写错位置被看出来，memcpy 会静默把后面所有字段错位。
 */
typedef struct {
    uint32_t          version;
    cw_wifi_profile_t profiles[CW_WIFI_PROFILE_MAX];
    char              host[64];
    uint16_t          port;
    int16_t           wire;
    char              office[16];
    uint16_t          rx_delay_ms;
    uint8_t           echo_mode;
    uint8_t           noise_notice_shown;
} cw_net_cfg_v1_t;

/* v2：v1 末尾加了 ota_url */
typedef struct {
    cw_net_cfg_v1_t   v1;
    char              ota_url[96];
} cw_net_cfg_v2_t;

static void migrate_v1(const cw_net_cfg_v1_t *old, cw_net_cfg_t *dst)
{
    set_defaults(dst);
    dst->version = CW_NET_CFG_VERSION;
    memcpy(dst->profiles, old->profiles, sizeof dst->profiles);
    snprintf(dst->host, sizeof dst->host, "%s", old->host);
    dst->port = old->port;
    dst->wire = old->wire;
    snprintf(dst->office, sizeof dst->office, "%s", old->office);
    dst->rx_delay_ms = old->rx_delay_ms;
    dst->echo_mode = old->echo_mode;
    dst->noise_notice_shown = old->noise_notice_shown;
    /* ota_url / boot_check 保持 set_defaults() 给的默认值 */
}

static void migrate_v2(const cw_net_cfg_v2_t *old, cw_net_cfg_t *dst)
{
    migrate_v1(&old->v1, dst);
    dst->version = CW_NET_CFG_VERSION;
    /* v2 的 ota_url 指向 .bin，v3 起指向版本清单 .json。旧值搬过来会让
       检查更新一直失败，所以只在它已经是清单地址时才保留。 */
    size_t n = strlen(old->ota_url);
    if (n > 5 && strcmp(old->ota_url + n - 5, ".json") == 0)
        snprintf(dst->ota_url, sizeof dst->ota_url, "%s", old->ota_url);
}

static void copy_field(char *dst, size_t dst_sz, const uint8_t *src, size_t src_len)
{
    size_t n = 0;
    while (n < src_len && n < dst_sz - 1 && src[n] != 0) n++;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static void set_defaults(cw_net_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = CW_NET_CFG_VERSION;
    snprintf(c->host, sizeof c->host, "%s", CW_NET_DEFAULT_HOST);
    c->port = CW_NET_DEFAULT_PORT;
    c->wire = CW_NET_DEFAULT_WIRE;
    c->rx_delay_ms = 500;
    snprintf(c->ota_url, sizeof c->ota_url, "%s", CW_NET_DEFAULT_OTA);
    c->echo_mode = 0;
    c->boot_check = 1;
    /* office ID 跟随呼号。cw_settings_init() 必须先于本函数调用过。 */
    const cw_settings_t *s = cw_settings_get();
    if (s && s->callsign[0]) snprintf(c->office, sizeof c->office, "%s", s->callsign);
}

static esp_err_t write_blob(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, NVS_KEY_NET_CFG, &s_cfg, sizeof s_cfg);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t net_store_init(void)
{
    set_defaults(&s_cfg);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s - running with defaults", esp_err_to_name(err));
        return err;
    }

    union {
        cw_net_cfg_t    cur;
        cw_net_cfg_v2_t v2m;
        cw_net_cfg_v1_t v1;
    } raw;
    size_t len = sizeof raw;
    err = nvs_get_blob(h, NVS_KEY_NET_CFG, &raw, &len);
    nvs_close(h);

    if (err == ESP_OK && len == sizeof(cw_net_cfg_v2_t) && raw.v2m.v1.version == 2) {
        ESP_LOGI(TAG, "migrating net cfg v2 -> v%d（保住已存的 WiFi）", CW_NET_CFG_VERSION);
        migrate_v2(&raw.v2m, &s_cfg);
        return write_blob();
    }
    if (err == ESP_OK && len == sizeof(cw_net_cfg_v1_t) && raw.v1.version == 1) {
        ESP_LOGI(TAG, "migrating net cfg v1 -> v%d（保住已存的 WiFi）", CW_NET_CFG_VERSION);
        migrate_v1(&raw.v1, &s_cfg);
        return write_blob();
    }
    if (err == ESP_OK && len == sizeof s_cfg) memcpy(&s_cfg, &raw.cur, sizeof s_cfg);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no net cfg, first run");
        set_defaults(&s_cfg);
        return write_blob();
    }
    if (err != ESP_OK || len != sizeof s_cfg || s_cfg.version != CW_NET_CFG_VERSION) {
        (void)0;
        /* 只重置本结构体。cw_settings 那边一个字节都不碰——用户的 Koch
           进度与这里无关，不能被网络配置的版本变更牵连。 */
        ESP_LOGW(TAG, "net cfg invalid (err=%s len=%u ver=%lu), reset",
                 esp_err_to_name(err), (unsigned)len, (unsigned long)s_cfg.version);
        set_defaults(&s_cfg);
        return write_blob();
    }

    ESP_LOGI(TAG, "net cfg loaded: %s:%u wire=%d office=%s delay=%ums echo=%u",
             s_cfg.host, s_cfg.port, s_cfg.wire, s_cfg.office,
             s_cfg.rx_delay_ms, s_cfg.echo_mode);
    return ESP_OK;
}

cw_net_cfg_t *net_store_get(void) { return &s_cfg; }

esp_err_t net_store_save(void) { return write_blob(); }

bool net_store_has_wifi(void)
{
    for (int i = 0; i < CW_WIFI_PROFILE_MAX; i++) {
        if (s_cfg.profiles[i].used && s_cfg.profiles[i].ssid[0]) return true;
    }
    return false;
}

/* 读出 fmo 的 blob 并校验。成功时 out 填满 FMO_BLOB_SIZE 字节。 */
static bool read_fmo_blob(uint8_t *out)
{
    nvs_handle_t h;
    if (nvs_open(FMO_NS, NVS_READONLY, &h) != ESP_OK) return false;

    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, FMO_KEY, NULL, &len);
    if (err != ESP_OK || len != FMO_BLOB_SIZE) {
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "fmo blob size %u != expected %d, layout changed? skip import",
                     (unsigned)len, FMO_BLOB_SIZE);
        }
        nvs_close(h);
        return false;
    }

    len = FMO_BLOB_SIZE;
    err = nvs_get_blob(h, FMO_KEY, out, &len);
    nvs_close(h);
    if (err != ESP_OK) return false;

    uint32_t ver = (uint32_t)out[0] | ((uint32_t)out[1] << 8) |
                   ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 24);
    if (ver != FMO_VERSION) {
        ESP_LOGW(TAG, "fmo blob version %lu != %d, skip import",
                 (unsigned long)ver, FMO_VERSION);
        return false;
    }
    return true;
}

static int count_fmo_profiles(const uint8_t *blob)
{
    int n = 0;
    for (int i = 0; i < FMO_PROFILE_MAX; i++) {
        const uint8_t *p = blob + FMO_PROFILES_OFF + i * FMO_PROFILE_SIZE;
        if (p[FMO_PROF_USED_OFF] && p[FMO_PROF_SSID_OFF]) n++;
    }
    return n;
}

bool net_store_fmo_available(void)
{
    uint8_t blob[FMO_BLOB_SIZE];
    if (!read_fmo_blob(blob)) return false;
    return count_fmo_profiles(blob) > 0;
}

int net_store_import_from_fmo(void)
{
    uint8_t blob[FMO_BLOB_SIZE];
    if (!read_fmo_blob(blob)) return 0;

    int dst = 0;
    for (int i = 0; i < FMO_PROFILE_MAX && dst < CW_WIFI_PROFILE_MAX; i++) {
        const uint8_t *p = blob + FMO_PROFILES_OFF + i * FMO_PROFILE_SIZE;
        if (!p[FMO_PROF_USED_OFF] || !p[FMO_PROF_SSID_OFF]) continue;

        cw_wifi_profile_t *d = &s_cfg.profiles[dst];
        memset(d, 0, sizeof *d);
        copy_field(d->ssid, sizeof d->ssid, p + FMO_PROF_SSID_OFF, FMO_PROF_SSID_LEN);
        copy_field(d->password, sizeof d->password, p + FMO_PROF_PASS_OFF, FMO_PROF_PASS_LEN);
        d->used = 1;
        dst++;
    }

    if (dst > 0) {
        ESP_LOGI(TAG, "imported %d wifi profile(s) from fmo-companion", dst);
        net_store_save();
    }
    return dst;
}
