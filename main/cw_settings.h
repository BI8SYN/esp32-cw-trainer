#ifndef CW_SETTINGS_H
#define CW_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CW_SETTINGS_VERSION 2

typedef struct {
    uint32_t version;
    uint8_t  char_wpm;
    uint8_t  eff_wpm;
    uint16_t tone_hz;
    uint8_t  volume;
    /*
     * 已废弃，不再读写，**但不能删**。
     *
     * 它曾经是「电键输入」二选一（触摸屏 / 外接口）；现在两路是并联的，设置项
     * 已经删掉。删掉这个字段就要升到 version 3，而 v3 与 v2 的 sizeof 很可能仍然
     * 相同——v1 和 v2 正是这样（见下方 cw_settings_v1_t 的说明：v2 追加的那个
     * uint8_t 落进了原来的尾部填充里，两者都是 68 字节）。再加一个同长度的版本，
     * cw_settings_init() 里那几条按 len 分派的迁移分支就分辨不出彼此了，赌注是
     * 用户的 Koch 进度。留一个有名字、有注释的死字节便宜得多。
     */
    uint8_t  reserved_key_input;
    uint8_t  keyer_mode;
    uint8_t  paddle_swap;
    char     callsign[12];
    uint8_t  koch_lesson;
    uint8_t  koch_last_pct[41];
    uint8_t  brightness;
    uint8_t  rotate_180;   /* 屏幕上下翻转；板子没有重力传感器，只能手动设置 */
} cw_settings_t;

esp_err_t      cw_settings_init(void);
cw_settings_t *cw_settings_get(void);
esp_err_t      cw_settings_save(void);
esp_err_t      cw_settings_reset(void);
bool           cw_settings_was_reset(void);

#endif
