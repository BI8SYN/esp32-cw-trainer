#ifndef CW_SETTINGS_H
#define CW_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CW_SETTINGS_VERSION 1
#define CW_KEY_INPUT_TOUCH  0
#define CW_KEY_INPUT_EXT    1

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
} cw_settings_t;

esp_err_t      cw_settings_init(void);
cw_settings_t *cw_settings_get(void);
esp_err_t      cw_settings_save(void);
esp_err_t      cw_settings_reset(void);
bool           cw_settings_was_reset(void);

#endif
