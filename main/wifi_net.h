#ifndef WIFI_NET_H
#define WIFI_NET_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * WiFi 站点管理。与 fmo-companion 的 wifi_manager 同源，但砍掉了 WebSocket、
 * 重连退避、WireGuard 相关的部分，并且**默认关闭射频**。
 *
 * spec 3.5 原来定的是「按需开关，默认关闭」，三条理由：
 *   1. 侧音走 ESP32 内置 DAC，是模拟输出。WiFi 发射的电流尖峰很可能在耳机里
 *      听成周期性底噪。离线练习（Koch 听写、电键）是主线场景，不能被可选的
 *      联网功能拖累音质。
 *   2. WiFi TX 峰值 250~300mA，随身盒子的续航会明显缩水。
 *   3. WiFi + lwIP 常驻约 50~60KB 堆，不用时还回去。
 *
 * 第 1 条已经实测否掉（2026-08-28）：联网对练全程听不出可闻底噪，而那正是
 * 发射最密集的场景——常连待机只会更轻。spec 473 行把这条列为必测项、挂了
 * 很久没测，就是它撑着整个「默认关闭」的设计。
 *
 * 所以现在是无条件常连：开机就连、掉线自动重连、任何页面都不关射频。射频
 * 一旦开起来就不再关闭，因此本模块没有 stop——省下的那 57KB（实测
 * 164008 -> 105752）和 TX 电流，换的是进对练页不用干等几秒握手。
 *
 * wifi_net_start() 才真正 esp_wifi_init()。它是幂等的：已经连上时直接返回，
 * 不会把一条好端端的连接掐掉重来。
 *
 * 回调在 ESP-IDF 的事件任务或本模块的工作任务里触发，**不是** LVGL 任务。
 * UI 侧必须用 ui_async_post() 转发，不能在回调里直接碰 lv_obj_t。
 */

typedef enum {
    WIFI_NET_OFF,
    WIFI_NET_SCANNING,
    WIFI_NET_CONNECTING,
    WIFI_NET_CONNECTED,
    WIFI_NET_FAIL_NO_AP,      /* 扫不到任何已保存的网络 */
    WIFI_NET_FAIL_AUTH,       /* 扫到了但连不上（多半是密码错） */
} wifi_net_state_t;

/* ssid 在 SCANNING/FAIL_NO_AP 时为空串。 */
typedef void (*wifi_net_cb_t)(wifi_net_state_t st, const char *ssid, void *user);

/* 只做 netif / event loop / STA netif 的一次性初始化，不开射频。 */
esp_err_t wifi_net_init(void);

/*
 * 开射频 -> 扫描 -> 连已保存的槽位（按信号从强到弱逐个试，不是只试最强的
 * 那一个）。非阻塞，进度走回调。
 *
 * 连上之后任务并不结束：它守着链路，掉线就重连，连不上就退避重试
 * （5 秒起、逐次加倍、封顶 60 秒）。「保持连接」不是「连上一次」——AP 重启、
 * 走出覆盖范围都会断链，而 ESP-IDF 不会自己重连。
 *
 * 幂等：已经连上时直接返回。各处都会「先 start 一下再用」，不挡住的话每次
 * 都会重新扫描重连，把连接掐掉几秒。
 */
esp_err_t wifi_net_start(wifi_net_cb_t cb, void *user);

bool             wifi_net_is_connected(void);
wifi_net_state_t wifi_net_state(void);
/* 已连接时返回 SSID，否则空串。 */
const char      *wifi_net_ssid(void);
/* 信号格数 0~3；未连接返回 -1。顶栏图标用。 */
int              wifi_net_level(void);

/*
 * 指定接下来要连哪个槽位。传 -1 恢复默认行为（扫描后选信号最强的已存槽位）。
 *
 * 加这个是因为原来只能自动选最强的，而"最强"未必是你想用的——比如手机
 * 热点信号弱但流量自由，家里的路由强却在跑别的事。设置页据此提供切换。
 */
/* 换槽位时会主动断开一次，让重连那一轮带着新槽位重新挑——射频常连之后
   不这么做的话，用户在设置里换了网络会毫无反应。 */
void             wifi_net_prefer(int slot);
int              wifi_net_preferred(void);

/*
 * 配网页用：同步扫描一次，把 SSID 去重后写进 out。返回条数。
 * 射频没开的话会自己开。
 * 这个函数会阻塞数秒，只能在非 LVGL 任务里调。
 */
int wifi_net_scan(char out[][33], int max_n);

#endif
