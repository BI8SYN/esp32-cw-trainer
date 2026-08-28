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

/* 在后台任务里执行整个流程，非阻塞。进度轮询 ota_status()。 */
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
