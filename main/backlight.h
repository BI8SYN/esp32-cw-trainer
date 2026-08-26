#ifndef BACKLIGHT_H
#define BACKLIGHT_H
#include <stdint.h>
void backlight_init(void);
void backlight_set(uint8_t percent);
#endif
