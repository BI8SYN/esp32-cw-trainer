#ifndef TRAINER_H
#define TRAINER_H
#include <stdbool.h>
#include <stdint.h>
#include "rng.h"
#include "callsign_gen.h"

typedef enum { TRAINER_MODE_KOCH = 0, TRAINER_MODE_CALLSIGN = 1, TRAINER_MODE_QSO = 2 } trainer_mode_t;

#define TRAINER_ROUNDS     10
#define TRAINER_MAX_ERRORS 16

typedef struct { char expected; char got; } trainer_error_t;

typedef struct {
    trainer_mode_t mode;
    uint8_t lesson;
    bool charset_fallback;
    rng_t rng;
    uint8_t round;
    uint8_t correct_rounds;
    char target[CALLSIGN_MAX];
    char rst[4];
    char options[4][CALLSIGN_MAX];
    char rst_options[4][4];
    char play_text[48];
    trainer_error_t errors[TRAINER_MAX_ERRORS];
    uint8_t error_count;
    bool finished;
} trainer_t;

void    trainer_start(trainer_t *t, trainer_mode_t mode, uint8_t lesson, uint32_t seed);
bool    trainer_submit(trainer_t *t, const char *answer);
bool    trainer_next(trainer_t *t);
uint8_t trainer_percent(const trainer_t *t);

#endif
