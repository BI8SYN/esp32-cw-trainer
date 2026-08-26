#ifndef TONE_GEN_H
#define TONE_GEN_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t tone_gen_init(void);
bool      tone_gen_is_ok(void);
void      tone_gen_set(bool on);
void      tone_gen_set_freq(uint16_t hz);
void      tone_gen_set_volume(uint8_t percent);

#endif
