#ifndef KEYER_H
#define KEYER_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KEYER_MODE_STRAIGHT = 0,
    KEYER_MODE_IAMBIC_A = 1,
    KEYER_MODE_IAMBIC_B = 2,
} keyer_mode_t;

enum { KEYER_ST_IDLE = 0, KEYER_ST_ELEMENT = 1, KEYER_ST_GAP = 2 };
enum { KEYER_EL_DIT = 0, KEYER_EL_DAH = 1 };

typedef struct {
    keyer_mode_t mode;
    uint16_t dit_ms;
    bool dit_in, dah_in;
    bool key_out;
    int state;
    int cur_el;
    bool mem_dit, mem_dah;
    uint32_t el_end_ms;
} keyer_t;

void keyer_init(keyer_t *k, keyer_mode_t mode, uint16_t dit_ms);
void keyer_set_mode(keyer_t *k, keyer_mode_t mode);
void keyer_set_dit_ms(keyer_t *k, uint16_t dit_ms);
void keyer_set_input(keyer_t *k, bool dit, bool dah);
/** 每 1~10ms 调用一次。返回当前输出电平。 */
bool keyer_tick(keyer_t *k, uint32_t now_ms);

#endif
