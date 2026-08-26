#include "callsign_gen.h"
#include <string.h>

const char CALLSIGN_FULL_CHARSET[37] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static const char CONFUSABLE[][2] = {
    {'S','H'}, {'U','V'}, {'E','I'}, {'N','M'}, {'D','B'},
    {'G','W'},  /* 反转混淆：--.  vs  .-- */
    {'K','R'},  /* 补偿混淆：-.-  vs  .-. */
    {'5','S'}, {'0','O'}, {'1','J'}, {'9','0'}, {'2','3'},
    {'T','M'}, {'6','5'},
};

static const char *RST_POOL[] = { "599", "579", "559", "539", "449", "339" };
#define RST_POOL_N 6

static int split(const char *charset, char *letters, char *digits)
{
    int nl = 0, nd = 0;
    for (const char *p = charset; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') {
            if (nl < 39) letters[nl++] = *p;
        } else if (*p >= '0' && *p <= '9') {
            if (nd < 11) digits[nd++] = *p;
        }
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
    char cands[8]; int n = 0;
    for (size_t i = 0; i < sizeof(CONFUSABLE) / sizeof(CONFUSABLE[0]); i++) {
        if (n < 8) {
            if (CONFUSABLE[i][0] == c) cands[n++] = CONFUSABLE[i][1];
            else if (CONFUSABLE[i][1] == c) cands[n++] = CONFUSABLE[i][0];
        }
    }
    if (n == 0) return '\0';
    return cands[rng_below(rng, (uint32_t)n)];
}

/* 与 c 同类（数字换数字、字母换字母）且不等于 c 的随机字符。
 * 干扰项必须保持呼号形状：数字位换成字母会让呼号失去唯一的数字，
 * 字母位换成数字则可能把数字放到首位或末位——两种都能让人不用听就排除掉。 */
static char type_preserving_other(char c, rng_t *rng)
{
    bool is_digit = (c >= '0' && c <= '9');
    char r;
    do {
        r = is_digit ? (char)('0' + rng_below(rng, 10))
                     : (char)('A' + rng_below(rng, 26));
    } while (r == c);
    return r;
}

static bool already_taken(const char *cand, char out[3][CALLSIGN_MAX], int made)
{
    for (int i = 0; i < made; i++) {
        if (strcmp(out[i], cand) == 0) return true;
    }
    return false;
}

void callsign_gen_distractors(const char *target, rng_t *rng, char out[3][CALLSIGN_MAX])
{
    size_t len = strlen(target);
    if (len == 0) {
        for (int i = 0; i < 3; i++) out[i][0] = '\0';
        return;
    }
    int made = 0;
    int guard = 0;
    while (made < 3 && guard++ < 200) {
        char cand[CALLSIGN_MAX];
        strncpy(cand, target, CALLSIGN_MAX - 1);
        cand[CALLSIGN_MAX - 1] = '\0';
        size_t pos = rng_below(rng, (uint32_t)len);
        char rep = confusable_of(target[pos], rng);
        if (!rep) rep = type_preserving_other(target[pos], rng);
        /* 保持"恰好一个数字"的形状：数字位只换数字，字母位只换字母 */
        bool tgt_digit = (target[pos] >= '0' && target[pos] <= '9');
        bool rep_digit = (rep >= '0' && rep <= '9');
        if (tgt_digit != rep_digit) rep = tgt_digit ? (char)('0' + rng_below(rng, 10)) : (char)('A' + rng_below(rng, 26));
        if (rep == target[pos]) continue;
        cand[pos] = rep;
        if (already_taken(cand, out, made)) continue;
        strcpy(out[made++], cand);
    }
    /* 极端情况兜底：依次改第 0、1、2 位 */
    for (; made < 3; made++) {
        size_t pos = made % len;
        char cand[CALLSIGN_MAX];
        int tries = 0;
        do {
            strncpy(cand, target, CALLSIGN_MAX - 1);
            cand[CALLSIGN_MAX - 1] = '\0';
            cand[pos] = type_preserving_other(target[pos], rng);
            tries++;
        } while (tries < 20 && already_taken(cand, out, made));
        strcpy(out[made], cand);
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
