#include "keyer.h"

void keyer_init(keyer_t *k, keyer_mode_t mode, uint16_t dit_ms)
{
    k->mode = mode;
    k->dit_ms = dit_ms ? dit_ms : 60;
    k->dit_in = k->dah_in = false;
    k->key_out = false;
    k->state = KEYER_ST_IDLE;
    k->cur_el = KEYER_EL_DIT;
    k->mem_dit = k->mem_dah = false;
    k->el_end_ms = 0;
}

void keyer_set_mode(keyer_t *k, keyer_mode_t mode) { keyer_init(k, mode, k->dit_ms); }
void keyer_set_dit_ms(keyer_t *k, uint16_t dit_ms) { if (dit_ms) k->dit_ms = dit_ms; }

void keyer_set_input(keyer_t *k, bool dit, bool dah)
{
    k->dit_in = dit;
    k->dah_in = dah;
}

static void start_element(keyer_t *k, int el, uint32_t now)
{
    k->cur_el = el;
    k->state = KEYER_ST_ELEMENT;
    k->key_out = true;
    k->el_end_ms = now + (el == KEYER_EL_DIT ? k->dit_ms : (uint32_t)k->dit_ms * 3);
    k->mem_dit = k->mem_dah = false;
}

bool keyer_tick(keyer_t *k, uint32_t now)
{
    if (k->mode == KEYER_MODE_STRAIGHT) {
        k->key_out = k->dit_in;
        return k->key_out;
    }

    /* 记忆：当前元素的对侧桨在元素/间隔期间被按下 */
    if (k->state != KEYER_ST_IDLE) {
        if (k->cur_el == KEYER_EL_DIT && k->dah_in) k->mem_dah = true;
        if (k->cur_el == KEYER_EL_DAH && k->dit_in) k->mem_dit = true;
    }

    switch (k->state) {
    case KEYER_ST_IDLE:
        if (k->dit_in) start_element(k, KEYER_EL_DIT, now);
        else if (k->dah_in) start_element(k, KEYER_EL_DAH, now);
        break;

    case KEYER_ST_ELEMENT:
        if (now >= k->el_end_ms) {
            k->key_out = false;
            k->state = KEYER_ST_GAP;
            k->el_end_ms = now + k->dit_ms;
        }
        break;

    case KEYER_ST_GAP:
        if (now >= k->el_end_ms) {
            int next = -1;
            if (k->dit_in && k->dah_in) next = (k->cur_el == KEYER_EL_DIT) ? KEYER_EL_DAH : KEYER_EL_DIT;
            else if (k->dit_in) next = KEYER_EL_DIT;
            else if (k->dah_in) next = KEYER_EL_DAH;
            else if (k->mode == KEYER_MODE_IAMBIC_B) {
                if (k->mem_dit) next = KEYER_EL_DIT;
                else if (k->mem_dah) next = KEYER_EL_DAH;
            }
            if (next >= 0) start_element(k, next, now);
            else { k->state = KEYER_ST_IDLE; k->mem_dit = k->mem_dah = false; }
        }
        break;
    }
    return k->key_out;
}
