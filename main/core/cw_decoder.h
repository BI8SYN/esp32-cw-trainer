#ifndef CW_DECODER_H
#define CW_DECODER_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t dit_ref_ms;
    bool key_down;
    uint32_t last_edge_ms;
    char pattern[8];
    uint8_t pattern_len;
    bool char_pending;
    bool word_gap_emitted;
} cw_decoder_t;

void cw_decoder_init(cw_decoder_t *d, uint16_t dit_ref_ms);
/* cw_decoder_set_dit_ref() 曾存在于此、允许外部重置基准点长，最终审查时发现
 * 零调用方——解码器已经靠 adapt()（cw_decoder.c 内部）在正常解码过程中持续
 * 自适应基准，外部手动重设从未被真正用到——已删除。 */
void cw_decoder_key(cw_decoder_t *d, bool down, uint32_t now_ms);
int cw_decoder_poll(cw_decoder_t *d, uint32_t now_ms, char *out);
const char *cw_decoder_pattern(const cw_decoder_t *d);

#endif
