#include "cw_decoder.h"
#include "morse_table.h"

#define DIT_REF_MIN 20
#define DIT_REF_MAX 400

void cw_decoder_init(cw_decoder_t *d, uint16_t dit_ref_ms)
{
    d->dit_ref_ms = dit_ref_ms ? dit_ref_ms : 60;
    d->key_down = false;
    d->last_edge_ms = 0;
    d->pattern[0] = '\0';
    d->pattern_len = 0;
    d->char_pending = false;
    d->word_gap_emitted = true;   /* 开机不先输出空格 */
}

void cw_decoder_set_dit_ref(cw_decoder_t *d, uint16_t dit_ref_ms)
{
    if (dit_ref_ms) d->dit_ref_ms = dit_ref_ms;
}

static void adapt(cw_decoder_t *d, uint32_t dit_sample)
{
    uint32_t ref = ((uint32_t)d->dit_ref_ms * 3 + dit_sample) / 4;
    if (ref < DIT_REF_MIN) ref = DIT_REF_MIN;
    if (ref > DIT_REF_MAX) ref = DIT_REF_MAX;
    d->dit_ref_ms = (uint16_t)ref;
}

void cw_decoder_key(cw_decoder_t *d, bool down, uint32_t now)
{
    if (down == d->key_down) return;
    if (!down) {
        uint32_t len = now - d->last_edge_ms;
        char el;
        if (len < (uint32_t)d->dit_ref_ms * 2) { el = '.'; adapt(d, len); }
        else el = '-';
        if (d->pattern_len < sizeof(d->pattern) - 1) {
            d->pattern[d->pattern_len++] = el;
            d->pattern[d->pattern_len] = '\0';
        }
        d->char_pending = true;
        d->word_gap_emitted = false;
    }
    d->key_down = down;
    d->last_edge_ms = now;
}

int cw_decoder_poll(cw_decoder_t *d, uint32_t now, char *out)
{
    if (d->key_down) return 0;
    uint32_t gap = now - d->last_edge_ms;
    if (d->char_pending && gap >= (uint32_t)d->dit_ref_ms * 2) {
        char c = morse_table_decode(d->pattern);
        *out = c ? c : '?';
        d->pattern[0] = '\0';
        d->pattern_len = 0;
        d->char_pending = false;
        return 1;
    }
    if (!d->char_pending && !d->word_gap_emitted && gap >= (uint32_t)d->dit_ref_ms * 5) {
        *out = ' ';
        d->word_gap_emitted = true;
        return 1;
    }
    return 0;
}

const char *cw_decoder_pattern(const cw_decoder_t *d) { return d->pattern; }
