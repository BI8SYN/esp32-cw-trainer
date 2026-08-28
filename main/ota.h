#ifndef OTA_H
#define OTA_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * 通过 HTTP 拉取固件并写入另一个 OTA 槽。
 *
 * 分区见 partitions.csv：两个 1.7MB 的 app 槽轮换。裁剪字库前固件 2.2MB，
 * 双槽根本放不下——所以字库裁剪是这个功能的前提，不是顺带的优化。
 *
 * 回滚策略：写完并校验通过才 set_boot_partition，然后重启。新固件启动后由
 * app_main 调 ota_mark_valid() 确认。**没确认就断电或崩溃的话，bootloader
 * 会自动回滚到旧槽**——这是 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 提供的
 * 保障，也是敢让用户自己刷的唯一理由。
 */

typedef enum {
    OTA_IDLE,
    OTA_CONNECTING,
    OTA_DOWNLOADING,
    OTA_DONE,       /* 写完待重启 */
    OTA_FAIL,
} ota_state_t;

typedef struct {
    ota_state_t state;
    int         percent;      /* 0~100，长度未知时为 -1 */
    char        msg[64];
} ota_status_t;

/*
 * 版本检查。设备只知道自己在跑哪个版本，要判断"有没有新的"必须问服务器，
 * 所以服务器上除了固件本身还要放一份清单：
 *
 *     http://.../cw-trainer.json
 *     {"version":"v1.3.0","url":"http://.../cw-trainer-v1.3.0.bin"}
 *
 * 固件文件名带版本号，一来能同时保留多个版本、二来出问题时看文件名就知道
 * 设备装的是哪个。
 */
typedef struct {
    bool checked;              /* 查过了（不论结果） */
    bool available;            /* 服务器版本与当前不同 */
    char latest[24];           /* 服务器上的版本号 */
    char bin_url[128];
    char err[48];              /* 查询失败的原因，成功时为空 */
} ota_check_t;

/* 后台拉清单并比对，非阻塞。结果轮询 ota_check_result()。 */
esp_err_t          ota_check_start(void);
/* stop_wifi_after=true 时查完自己关射频——开机检查用，否则就成了 WiFi 常连。 */
esp_err_t          ota_check_start_auto(bool stop_wifi_after);
const ota_check_t *ota_check_result(void);
bool               ota_check_busy(void);
/* 当前固件版本，来自 esp_app_get_description()。 */
const char        *ota_current_version(void);

/* 在后台任务里执行整个流程，非阻塞。进度轮询 ota_status()。
   url 传 NULL 时用上一次检查得到的 bin_url。 */
esp_err_t ota_start(const char *url);
const ota_status_t *ota_status(void);
/* OTA_DONE 之后调用，重启进入新固件。 */
void      ota_reboot(void);

/*
 * 启动时调用：如果本次是从刚升级的槽第一次启动，把它标记为可用。
 * 不调用的话 bootloader 在下次复位时会回滚。
 */
void      ota_mark_valid(void);
/* 当前运行在哪个槽，给设置页显示用。 */
const char *ota_running_label(void);

#endif
