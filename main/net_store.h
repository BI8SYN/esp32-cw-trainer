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
    /*
     * 这 16 字节原来是 office（站点 ID），已废弃——站点 ID 现在每次上线现取
     * 呼号，见 cw_net_office()。
     *
     * 它废掉的原因值得留着：office 是 set_defaults() 里从呼号拷的一份副本，
     * 而 set_defaults() 只在首次初始化（NVS 里还没有 blob）时跑一次，那一刻
     * 呼号还是出厂默认。用户后来改呼号，改的是 cw_settings，这份副本一动
     * 不动——于是**所有设备上线报出去的 ID 永远是出厂值**。
     *
     * 现在这块地方改放「用户亲自定过呼号」的标记（CALLSIGN_OK_MAGIC），用来
     * 让开机清理遗留出厂呼号那一步只发生一次，见 cw_net_callsign_confirm()。
     *
     * 复用而不是新加字段：加字段会改变结构体长度，就要提版本号写迁移，而每
     * 改一次这个 blob 都是一次冲掉用户 WiFi 密码的机会（v1->v2 那次就冲了）。
     * 老设备这里存的是旧的 office 值（"BI8SYN" 之类），不等于 magic，正好被
     * 当成「还没定过」——升级路径不用额外处理。
     */
    char              callsign_ok[16];
    uint16_t          rx_delay_ms;
    /*
     * 已废弃，不再读写。
     *
     * 它曾经表示"陪练模式"，选回声时强制走 CW_NET_ECHO_WIRE——于是 wire 被
     * 两个设置控制着，而这个覆盖关系在界面上完全看不出来（你设了 wire 5，
     * 选回声却跑到了 12）。现在 wire 是唯一的控制项，"陪练模式"本来也只是
     * "换哪条 wire"的别名。
     *
     * 保留这个字节而不是删掉：删了结构体就变短，又要提版本号写迁移，而每
     * 一次改这个 blob 都是一次冲掉用户 WiFi 密码的机会（v1->v2 那次就冲了）。
     * 留着不用，代价是一个字节。
     */
    /*
     * 已废弃，不再读写。曾经是「陪练模式」，后来短暂做过「保持联网」开关。
     * 射频现在无条件常连，没有开关了。字节保留不删，理由同其他废弃字段：
     * 改结构体长度就要提版本号写迁移，而每改一次这个 blob 都是一次冲掉用户
     * WiFi 密码的机会（v1->v2 那次就冲了）。
     */
    uint8_t           unused_byte;
    /* 已废弃，不再读写。它曾经记「侧音底噪的提示是否展示过」，那条提示占着
       收报记录的第一行，而绝大多数人第一次进来看到的就是它——想练的东西被
       一句注意事项挡在前面。字节保留不删，理由同上面那几个废弃字段。 */
    uint8_t           noise_notice_deprecated;
    char              ota_url[96];          /* 版本清单（JSON）地址 */
    uint8_t           boot_check;          /* 开机联网检查一次更新 */
} cw_net_cfg_t;

/* v2 加了 ota_url，v3 加了 boot_check。每次改结构体都必须提版本号**并写迁移**
   ——v1->v2 那次我只提了版本号没写迁移，把用户存的 WiFi 账号密码全冲了。 */
#define CW_NET_CFG_VERSION 3

/* 默认服务器是自建的那台（阿里云广州）。不接国际公共 wire：实测国际线路
   UDP 丢包 17~24%，而协议是每字符一个包且无重传，等于每五个字符丢一个。
   wire 默认 11——必须避开 1844 / 1850，服务端 pins.py 的历史 PIN 会让那
   两条 wire 在客户端不带匹配 PIN 时静默拒绝中继。 */
#define CW_NET_DEFAULT_HOST  "8.138.130.141"
#define CW_NET_DEFAULT_PORT  7890
#define CW_NET_DEFAULT_WIRE  11
/*
 * 陪练机器人各占一条 wire。机器人跑在服务端、是 wire 上的普通客户端，
 * 读不到设备里的任何设置，所以用协议自己的频道机制区分行为，不扩协议。
 *
 * wire 号对用户没有意义，**上面有什么才有意义**——界面上一律显示
 * cw_net_wire_label() 给出的名字。
 */
#define CW_NET_QSO_WIRE      11    /* cwbot-qso，身份 BOT */
#define CW_NET_ECHO_WIRE     12    /* cwbot-echo，身份 ECHO */

/* 返回这条 wire 上是什么。未知的 wire 返回 NULL，由调用方显示 "wire N"。 */
const char *cw_net_wire_label(int16_t wire);

/*
 * 上线用的站点 ID。**直接取当前呼号，不留副本**——副本正是上面
 * office_deprecated 的教训：拷一次就永远停在出厂值，两台设备撞成同一个 ID，
 * 在 wire 上谁也认不出谁。呼号为空时退回 "CWTRN"，因为 ID 包不能发空串：
 * 服务器会把空 ID 的站点显示成一片空白，界面上分不清是谁。
 */
const char *cw_net_office(void);

/*
 * 呼号是否可用。联网对练用它做准入：没有就先弹窗要求定一个，因为呼号会作为
 * 站点 ID 上到 wire 上，而一条 wire 上两个同名站点是分不清谁是谁的。
 *
 * 判据就是「呼号非空」。老设备上遗留的出厂值 BI8SYN 由 net_store_init()
 * 开机清掉一次，所以这里不需要再对它做特判。
 */
bool cw_net_callsign_ok(void);

/*
 * 用户在呼号编辑框里按下确定时调用，写下 callsign_ok 标记并落盘。
 *
 * 标记只有一个用途：让开机清理出厂呼号那一步**只发生一次**。BI8SYN 是个
 * 合法呼号，真有人叫这个；他自己定过之后就不能再被清掉了。
 */
void cw_net_callsign_confirm(void);
/* 默认固件地址：wire 服务器那台机器上的静态页（morsekob-web 单元，端口 7891）。
   把固件放进 /opt/morsekob/www/ 即可。 */
/* 指向版本清单而不是固件本身：设备要先知道服务器上是什么版本，才谈得上
   "有没有新的"。清单里再给固件的实际地址。 */
#define CW_NET_DEFAULT_OTA   "http://8.138.130.141:7891/cw-trainer.json"

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
