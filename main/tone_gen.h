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

/*
 * 提前给功放上电并保持。
 *
 * GPIO4 是功放的关断脚，平时关着省电。原来是"第一个点到来的那一拍才打开
 * 功放，紧接着就把音频写出去"——功放还在上电稳定（耦合电容充电、偏置建立）
 * 的过程中，第一个 5ms 包络就已经在播了，听感就是第一声破音、听不清。
 * 之后因为有 200ms 关断延迟，同一个字符里的后续点划都正常，所以症状只出现
 * 在"第一声"。
 *
 * 所以要在**进入会发声的页面时**就打开，等用户真按下键，功放早已稳定。
 * 松开后由原有的 200ms 延迟自动关断，不用精确配对。
 */
void      tone_gen_amp_hold(bool hold);

#endif
