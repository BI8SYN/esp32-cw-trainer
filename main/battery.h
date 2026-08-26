#ifndef BATTERY_H
#define BATTERY_H
#include <stdint.h>
#include "esp_err.h"
typedef void (*battery_cb_t)(uint8_t percent, uint16_t mv);
esp_err_t battery_start(battery_cb_t cb);
uint8_t   battery_percent(void);
#endif
