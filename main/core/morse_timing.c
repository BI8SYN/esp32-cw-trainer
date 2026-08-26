#include "morse_timing.h"

static uint8_t clamp_wpm(uint8_t w)
{
    if (w < MORSE_WPM_MIN) return MORSE_WPM_MIN;
    if (w > MORSE_WPM_MAX) return MORSE_WPM_MAX;
    return w;
}

void morse_timing_compute(uint8_t char_wpm, uint8_t eff_wpm, morse_timing_t *out)
{
    uint32_t c = clamp_wpm(char_wpm);
    uint32_t s = clamp_wpm(eff_wpm);
    uint32_t dit = 1200 / c;

    out->dit_ms = (uint16_t)dit;
    out->dah_ms = (uint16_t)(dit * 3);
    out->intra_gap_ms = (uint16_t)dit;

    if (s >= c) {
        out->char_gap_ms = (uint16_t)(dit * 3);
        out->word_gap_ms = (uint16_t)(dit * 7);
        return;
    }
    uint32_t ta = (60000UL * c - 37200UL * s) / (s * c);
    out->char_gap_ms = (uint16_t)(ta * 3 / 19);
    out->word_gap_ms = (uint16_t)(ta * 7 / 19);
}
