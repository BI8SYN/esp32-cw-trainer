#ifndef NET_STORE_H
#define NET_STORE_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * 联网对练的持久化配置：WiFi 槽位 + 服务器设置。
 *
 * 这些字段**必须**存在独立的 NVS key 里，绝对不能并进 cw_settings_t。
 * 原因见 cw_settings.c 的加载逻辑：
 *
 *     if (err != ESP_OK || len != sizeof s_cfg || s_cfg.version != CW_SETTINGS_VERSION) {
 *         set_defaults(&s_cfg);   // 全部回默认值
 *     }
 *
 * 而 cw_settings_t 里装着 koch_lesson 和 koch_last_pct[41]——用户的全部
 * Koch 训练进度。往那个结构体里加字段（哪怕不改版本号，长度变了也一样）
 * 会让所有老用户升级固件后进度清零。
 *
 * 命名空间沿用 cwtrn，只是 key 不同，两个 blob 各自带版本号、互不影响。
 */

#define CW_WIFI_PROFILE_MAX 4

/*
 * 为什么是 4 个槽位：存储根本不是约束（每槽 98 字节，4 槽 392 字节，而 NVS
 * 分区有 24KB，存 20 个都无所谓）。真正的成本是在 320x240 电阻屏上用 LVGL
 * 键盘敲 WPA2 密码，槽位再多也不会有人填。4 是按随身设备的实际场景定的：
 * 家 / 单位或宿舍 / 手机热点（在外练习时唯一可用的网络）/ 流动点。
 * 上限不再加是因为横屏一屏能舒服地列 4 行，5 行就要滚动。
 */
typedef struct {
    uint8_t used;              /* 0 = 空槽 */
    char    ssid[33];          /* 32 字节 SSID + NUL */
    char    password[64];      /* 63 字节 WPA2 + NUL */
} cw_wifi_profile_t;

typedef struct {
    uint32_t          version;
    cw_wifi_profile_t profiles[CW_WIFI_PROFILE_MAX];
    char              host[64];
    uint16_t          port;
    int16_t           wire;
    char              office[16];
    uint16_t          rx_delay_ms;
    uint8_t           echo_mode;            /* 0 = 机器人 QSO(wire)，1 = 回声(CW_NET_ECHO_WIRE) */
    uint8_t           noise_notice_shown;   /* 侧音底噪提示是否已展示过 */
} cw_net_cfg_t;

#define CW_NET_CFG_VERSION 1

/* 默认服务器是自建的那台（阿里云广州）。不接国际公共 wire：实测国际线路
   UDP 丢包 17~24%，而协议是每字符一个包且无重传，等于每五个字符丢一个。
   wire 默认 11——必须避开 1844 / 1850，服务端 pins.py 的历史 PIN 会让那
   两条 wire 在客户端不带匹配 PIN 时静默拒绝中继。 */
#define CW_NET_DEFAULT_HOST  "8.138.130.141"
#define CW_NET_DEFAULT_PORT  7890
#define CW_NET_DEFAULT_WIRE  11
/*
 * 回声陪练单独占一条 wire。
 *
 * 机器人跑在服务端、是 wire 上一个普通客户端，读不到本机的 echo_mode。
 * 与其为此扩协议，不如直接用协议自己的频道机制：wire 11 上跑 QSO 陪练，
 * wire 12 上跑回声，echo_mode 只是决定接哪条。零额外状态。
 */
#define CW_NET_ECHO_WIRE     12

esp_err_t     net_store_init(void);
cw_net_cfg_t *net_store_get(void);
esp_err_t     net_store_save(void);
bool          net_store_has_wifi(void);

/* 从 fmo-companion 的 appcfg 命名空间导入 WiFi 槽位。
   返回导入的槽位数；0 表示没有可导入的（不是错误）。
   只读 appcfg，不写，不影响 fmo-companion。 */
int net_store_import_from_fmo(void);
/* 探测是否存在可导入的数据，不改变本侧任何状态——配网页用它决定要不要
   显示「从 FMO 伴侣导入」按钮。 */
bool net_store_fmo_available(void);

#endif
