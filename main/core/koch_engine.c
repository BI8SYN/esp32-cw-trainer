#include "koch_engine.h"
#include <string.h>

const char KOCH_ORDER[41] = "KMRSUAPTLOWI.NJEF0Y,VG5/Q9ZH38B?427C1D6X";

static uint8_t clamp_lesson(uint8_t l)
{
    if (l < KOCH_LESSON_MIN) return KOCH_LESSON_MIN;
    if (l > KOCH_LESSON_MAX) return KOCH_LESSON_MAX;
    return l;
}

int koch_charset(uint8_t lesson, char *out)
{
    int n = clamp_lesson(lesson) + 1;
    memcpy(out, KOCH_ORDER, (size_t)n);
    out[n] = '\0';
    return n;
}

char koch_new_char(uint8_t lesson) { return KOCH_ORDER[clamp_lesson(lesson)]; }

void koch_make_group(uint8_t lesson, rng_t *rng, char *out)
{
    char cs[48];
    int n = koch_charset(lesson, cs);
    for (int i = 0; i < KOCH_GROUP_LEN; i++) out[i] = cs[rng_below(rng, (uint32_t)n)];
    out[rng_below(rng, KOCH_GROUP_LEN)] = koch_new_char(lesson);
    out[KOCH_GROUP_LEN] = '\0';
}

bool koch_passed(uint8_t percent) { return percent >= KOCH_PASS_PERCENT; }

int koch_score_group(const char *target, const char *answer, uint8_t *mismatch)
{
    int correct = 0;
    uint8_t mm = 0;
    size_t alen = strlen(answer);
    for (int i = 0; target[i] && i < 8; i++) {
        if ((size_t)i < alen && answer[i] == target[i]) correct++;
        else mm |= (uint8_t)(1u << i);
    }
    if (mismatch) *mismatch = mm;
    return correct;
}
