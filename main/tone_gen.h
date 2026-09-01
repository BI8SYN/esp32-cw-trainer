#ifndef TONE_GEN_H
#define TONE_GEN_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "tone_seq.h"

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

/*
 * 播一段提示音（序列定义见 core/tone_seq.h）。
 *
 * **被占用就整段丢弃，不排队。** 排队意味着提示音会在一个随机的、和触发动作
 * 早已失去关联的时刻响起来，那比不响更糟；半截的提示音同理。
 *
 * 「被占用」有三种：CW 会话进行中（tone_gen_hold）、侧音刚响过还不到静音门闩、
 * 已经有一段提示音在播。判定与播放都在 tone_task 里，见 tone_gen.c。
 */
void      tone_gen_seq(const tone_step_t *steps, uint8_t n);

/*
 * CW 会话期间占住振荡器，提示音一律让路。
 *
 * 为什么光靠「安静了多久」不够：听写播放在**字符之间本来就有长静音**，
 * Farnsworth 间距下更长。纯靠静音时长判空闲，提示音会正好落进听写的字间隔里
 * ——那是最糟的位置。所以 cw_player 在整个播放会话期间把这个置上。
 */
void      tone_gen_hold(bool hold);

#endif
