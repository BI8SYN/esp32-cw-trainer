#ifndef WIFI_NET_H
#define WIFI_NET_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * WiFi 站点管理。与 fmo-companion 的 wifi_manager 同源，但砍掉了 WebSocket、
 * 重连退避、WireGuard 相关的部分，并且**默认关闭射频**。
 *
 * 为什么按需开关（spec 3.5）：
 *   1. 侧音走 ESP32 内置 DAC，是模拟输出。WiFi 发射的电流尖峰很可能在耳机里
 *      听成周期性底噪。离线练习（Koch 听写、电键）是主线场景，不能被可选的
 *      联网功能拖累音质。
 *   2. WiFi TX 峰值 250~300mA，随身盒子的续航会明显缩水。
 *   3. WiFi + lwIP 常驻约 50~60KB 堆，不用时还回去。
 *
 * 因此 wifi_net_start() 才真正 esp_wifi_init()，wifi_net_stop() 会
 * esp_wifi_deinit() 把内存还回去，而不是只 esp_wifi_stop()。
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

/* 开射频 -> 扫描 -> 连已保存槽位里信号最强的那个。非阻塞，进度走回调。 */
esp_err_t wifi_net_start(wifi_net_cb_t cb, void *user);

/* 断开、关射频、esp_wifi_deinit() 把内存还回去。 */
void wifi_net_stop(void);

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
void             wifi_net_prefer(int slot);
int              wifi_net_preferred(void);

/*
 * 配网页用：同步扫描一次，把 SSID 去重后写进 out。返回条数。
 * 必须在 wifi_net_start() 之后调用（射频要开着）。
 * 这个函数会阻塞数秒，只能在非 LVGL 任务里调。
 */
int wifi_net_scan(char out[][33], int max_n);

#endif
