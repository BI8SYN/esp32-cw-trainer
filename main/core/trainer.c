#include "trainer.h"
#include "koch_engine.h"
#include <stdio.h>
#include <string.h>

static void add_error(trainer_t *t, char expected, char got)
{
    if (t->error_count < TRAINER_MAX_ERRORS) {
        t->errors[t->error_count].expected = expected;
        t->errors[t->error_count].got = got;
        t->error_count++;
    }
}

static void shuffle_options(trainer_t *t, char (*opts)[CALLSIGN_MAX], int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)rng_below(&t->rng, (uint32_t)i + 1);
        /* i == j is a legitimate Fisher-Yates draw (self-swap, a no-op).
         * Skip the copies in that case: memcpy's parameters are `restrict`-
         * qualified, so src == dst is undefined behavior even though the
         * bytes are identical. Benign on some libcs, but not a property to
         * rely on -- do not "simplify" this guard away. */
        if (i == j) continue;
        char tmp[CALLSIGN_MAX];
        memcpy(tmp, opts[i], CALLSIGN_MAX);
        memcpy(opts[i], opts[j], CALLSIGN_MAX);
        memcpy(opts[j], tmp, CALLSIGN_MAX);
    }
}

static void gen_callsign_round(trainer_t *t)
{
    char cs[48];
    koch_charset(t->lesson, cs);
    t->charset_fallback = false;
    if (!callsign_gen_random(cs, &t->rng, t->target)) {
        t->charset_fallback = true;
        callsign_gen_random(CALLSIGN_FULL_CHARSET, &t->rng, t->target);
    }
    char d[3][CALLSIGN_MAX];
    callsign_gen_distractors(t->target, &t->rng, d);
    strcpy(t->options[0], t->target);
    for (int i = 0; i < 3; i++) strcpy(t->options[i + 1], d[i]);
    shuffle_options(t, t->options, 4);
}

static void gen_round(trainer_t *t)
{
    switch (t->mode) {
    case TRAINER_MODE_KOCH:
        koch_make_group(t->lesson, &t->rng, t->target);
        strcpy(t->play_text, t->target);
        break;
    case TRAINER_MODE_CALLSIGN:
        gen_callsign_round(t);
        strcpy(t->play_text, t->target);
        break;
    case TRAINER_MODE_QSO: {
        gen_callsign_round(t);
        char rsts[4][4];
        callsign_gen_rst(&t->rng, rsts);
        strcpy(t->rst, rsts[0]);
        for (int i = 0; i < 4; i++) strcpy(t->rst_options[i], rsts[i]);
        /* 打乱 RST 选项（rsts[0] 是正确项） */
        for (int i = 3; i > 0; i--) {
            int j = (int)rng_below(&t->rng, (uint32_t)i + 1);
            /* i == j is a legitimate Fisher-Yates draw (self-swap, a no-op).
             * Skip the copies in that case: strcpy's parameters are
             * `restrict`-qualified, so src == dst is undefined behavior --
             * and unlike memcpy this one is not merely benign in practice:
             * it traps (SIGTRAP) under this host's fortified libc. */
            if (i == j) continue;
            char tmp[4]; strcpy(tmp, t->rst_options[i]);
            strcpy(t->rst_options[i], t->rst_options[j]);
            strcpy(t->rst_options[j], tmp);
        }
        snprintf(t->play_text, sizeof t->play_text, "CQ CQ DE %s K UR RST %s %s BK", t->target, t->rst, t->rst);
        break;
    }
    }
}

void trainer_start(trainer_t *t, trainer_mode_t mode, uint8_t lesson, uint32_t seed)
{
    memset(t, 0, sizeof *t);
    t->mode = mode;
    t->lesson = lesson;
    rng_seed(&t->rng, seed);
    gen_round(t);
}

bool trainer_submit(trainer_t *t, const char *answer)
{
    bool ok;
    if (t->mode == TRAINER_MODE_KOCH) {
        uint8_t mm = 0;
        int correct = koch_score_group(t->target, answer, &mm);
        size_t alen = strlen(answer);
        for (int i = 0; t->target[i]; i++) {
            if (mm & (1u << i)) add_error(t, t->target[i], (size_t)i < alen ? answer[i] : '_');
        }
        ok = (correct == KOCH_GROUP_LEN);
    } else if (t->mode == TRAINER_MODE_CALLSIGN) {
        ok = strcmp(answer, t->target) == 0;
        if (!ok) {
            /* 记录第一个不同的字符 */
            size_t i = 0;
            while (t->target[i] && answer[i] && t->target[i] == answer[i]) i++;
            add_error(t, t->target[i] ? t->target[i] : '_', answer[i] ? answer[i] : '_');
        }
    } else {
        char call[CALLSIGN_MAX] = {0}, rst[4] = {0};
        sscanf(answer, "%7s %3s", call, rst);
        bool call_ok = strcmp(call, t->target) == 0;
        bool rst_ok = strcmp(rst, t->rst) == 0;
        if (!call_ok) {
            size_t i = 0;
            while (t->target[i] && call[i] && t->target[i] == call[i]) i++;
            add_error(t, t->target[i] ? t->target[i] : '_', call[i] ? call[i] : '_');
        }
        if (!rst_ok) add_error(t, t->rst[1], rst[1] ? rst[1] : '_');
        ok = call_ok && rst_ok;
    }
    if (ok) t->correct_rounds++;
    return ok;
}

bool trainer_next(trainer_t *t)
{
    if (t->round + 1 >= TRAINER_ROUNDS) {
        t->finished = true;
        return false;
    }
    t->round++;
    gen_round(t);
    return true;
}

uint8_t trainer_percent(const trainer_t *t)
{
    return (uint8_t)((uint32_t)t->correct_rounds * 100u / TRAINER_ROUNDS);
}
