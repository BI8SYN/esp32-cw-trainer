#include "callsign_gen.h"
#include <string.h>

const char CALLSIGN_FULL_CHARSET[37] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static const char CONFUSABLE[][2] = {
    {'S','H'}, {'U','V'}, {'E','I'}, {'N','M'}, {'D','B'}, {'G','W'},
    {'K','R'}, {'5','S'}, {'0','O'}, {'1','J'}, {'9','6'}, {'2','3'},
};

static const char *RST_POOL[] = { "599", "579", "559", "539", "449", "339" };
#define RST_POOL_N 6

static int split(const char *charset, char *letters, char *digits)
{
    int nl = 0, nd = 0;
    for (const char *p = charset; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') letters[nl++] = *p;
        else if (*p >= '0' && *p <= '9') digits[nd++] = *p;
    }
    letters[nl] = '\0'; digits[nd] = '\0';
    return nl;
}

bool callsign_gen_random(const char *charset, rng_t *rng, char out[CALLSIGN_MAX])
{
    char letters[40], digits[12];
    int nl = split(charset, letters, digits);
    int nd = (int)strlen(digits);
    if (nl < 4 || nd < 1) return false;

    int prefix = 1 + (int)rng_below(rng, 2);       /* 1..2 */
    int suffix = 1 + (int)rng_below(rng, 3);       /* 1..3 */
    int i = 0;
    for (int k = 0; k < prefix; k++) out[i++] = letters[rng_below(rng, (uint32_t)nl)];
    out[i++] = digits[rng_below(rng, (uint32_t)nd)];
    for (int k = 0; k < suffix; k++) out[i++] = letters[rng_below(rng, (uint32_t)nl)];
    out[i] = '\0';
    return true;
}

static char confusable_of(char c, rng_t *rng)
{
    char cands[4]; int n = 0;
    for (size_t i = 0; i < sizeof(CONFUSABLE) / sizeof(CONFUSABLE[0]); i++) {
        if (CONFUSABLE[i][0] == c) cands[n++] = CONFUSABLE[i][1];
        else if (CONFUSABLE[i][1] == c) cands[n++] = CONFUSABLE[i][0];
    }
    if (n == 0) return '\0';
    return cands[rng_below(rng, (uint32_t)n)];
}

static char any_other(char c, rng_t *rng)
{
    char r;
    do { r = CALLSIGN_FULL_CHARSET[rng_below(rng, 36)]; } while (r == c);
    return r;
}

void callsign_gen_distractors(const char *target, rng_t *rng, char out[3][CALLSIGN_MAX])
{
    size_t len = strlen(target);
    int made = 0;
    int guard = 0;
    while (made < 3 && guard++ < 200) {
        char cand[CALLSIGN_MAX];
        strncpy(cand, target, CALLSIGN_MAX - 1);
        cand[CALLSIGN_MAX - 1] = '\0';
        size_t pos = rng_below(rng, (uint32_t)len);
        char rep = confusable_of(target[pos], rng);
        if (!rep) rep = any_other(target[pos], rng);
        /* 保持"恰好一个数字"的形状：数字位只换数字，字母位只换字母 */
        bool tgt_digit = (target[pos] >= '0' && target[pos] <= '9');
        bool rep_digit = (rep >= '0' && rep <= '9');
        if (tgt_digit != rep_digit) rep = tgt_digit ? (char)('0' + rng_below(rng, 10)) : (char)('A' + rng_below(rng, 26));
        if (rep == target[pos]) continue;
        cand[pos] = rep;
        bool dup = false;
        for (int i = 0; i < made; i++) if (strcmp(out[i], cand) == 0) dup = true;
        if (dup) continue;
        strcpy(out[made++], cand);
    }
    /* 极端情况兜底：依次改第 0、1、2 位 */
    for (; made < 3; made++) {
        strcpy(out[made], target);
        out[made][made % len] = any_other(target[made % len], rng);
    }
}

void callsign_gen_rst(rng_t *rng, char out[4][4])
{
    int idx[RST_POOL_N];
    for (int i = 0; i < RST_POOL_N; i++) idx[i] = i;
    for (int i = RST_POOL_N - 1; i > 0; i--) {
        int j = (int)rng_below(rng, (uint32_t)i + 1);
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    for (int i = 0; i < 4; i++) strcpy(out[i], RST_POOL[idx[i]]);
}
