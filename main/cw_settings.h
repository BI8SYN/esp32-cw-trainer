#ifndef CW_SETTINGS_H
#define CW_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CW_SETTINGS_VERSION 4

/* 电量显示：主页顶栏右侧显示到什么程度。不接电池的人把它关掉，
   顶栏那一行会跟着重排，不会留下一块空白。 */
#define CW_BATT_HIDE   0   /* 不显示 */
#define CW_BATT_ICON   1   /* 只显示电池图标 */
#define CW_BATT_NUM    2   /* 图标 + 百分比数字（出厂默认） */

typedef struct {
    uint32_t version;
    uint8_t  char_wpm;
    uint8_t  eff_wpm;
    uint16_t tone_hz;
    uint8_t  volume;
    /*
     * v2 的这个位置是「电键输入」二选一（触摸屏 / 外接口）。两路改成并联之后
     * 设置项没了，字段一度保留成死字节以避开升版本；v3 加「电量显示」时既然
     * 已经要升版本，就把它真正删掉，结构体仍然是 68 字节。
     *
     * v2 -> v3 的迁移靠 **version 字段**分辨，不靠长度——v1/v2/v3 三者的 sizeof
     * 恰好都是 68，见下方 cw_settings_v1_t 的说明。
     */
    uint8_t  keyer_mode;
    uint8_t  paddle_swap;
    char     callsign[12];
    uint8_t  koch_lesson;
    uint8_t  koch_last_pct[41];
    uint8_t  brightness;
    uint8_t  rotate_180;   /* 屏幕上下翻转；板子没有重力传感器，只能手动设置 */
    uint8_t  battery_show; /* CW_BATT_*：不接电池的人可以整个关掉 */
    /*
     * v4 新增：界面提示音总开关，默认开。
     *
     * v1/v2/v3 的 sizeof 恰好都是 68（见下方那几个布局快照的说明），而 v4 多这
     * 一个字节之后是 72——**从 v4 起长度也能分辨版本了**，但判据仍然只看
     * version 字段，别依赖长度。
     */
    uint8_t  sound_on;
} cw_settings_t;

esp_err_t      cw_settings_init(void);
cw_settings_t *cw_settings_get(void);
esp_err_t      cw_settings_save(void);
esp_err_t      cw_settings_reset(void);
bool           cw_settings_was_reset(void);

#endif
