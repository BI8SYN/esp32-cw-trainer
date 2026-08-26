#ifndef KOCH_ENGINE_H
#define KOCH_ENGINE_H
#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

#define KOCH_LESSON_MIN   1
#define KOCH_LESSON_MAX   39
#define KOCH_GROUP_LEN    5
#define KOCH_PASS_PERCENT 90

extern const char KOCH_ORDER[41];

int  koch_charset(uint8_t lesson, char *out);
char koch_new_char(uint8_t lesson);
void koch_make_group(uint8_t lesson, rng_t *rng, char *out);
bool koch_passed(uint8_t percent);
int  koch_score_group(const char *target, const char *answer, uint8_t *mismatch);

#endif
