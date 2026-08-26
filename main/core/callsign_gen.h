#ifndef CALLSIGN_GEN_H
#define CALLSIGN_GEN_H
#include <stdbool.h>
#include "rng.h"

#define CALLSIGN_MAX 8

extern const char CALLSIGN_FULL_CHARSET[37];

bool callsign_gen_random(const char *charset, rng_t *rng, char out[CALLSIGN_MAX]);
void callsign_gen_distractors(const char *target, rng_t *rng, char out[3][CALLSIGN_MAX]);
void callsign_gen_rst(rng_t *rng, char out[4][4]);

#endif
