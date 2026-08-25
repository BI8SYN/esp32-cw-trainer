#ifndef MORSE_TIMING_H
#define MORSE_TIMING_H
#include <stdint.h>

typedef struct {
    uint16_t dit_ms;
    uint16_t dah_ms;
    uint16_t intra_gap_ms;
    uint16_t char_gap_ms;
    uint16_t word_gap_ms;
} morse_timing_t;

#define MORSE_WPM_MIN 5
#define MORSE_WPM_MAX 60

/** 由字符速度与有效速度（Farnsworth）计算各时长。参数钳到 5..60。 */
void morse_timing_compute(uint8_t char_wpm, uint8_t eff_wpm, morse_timing_t *out);

#endif
