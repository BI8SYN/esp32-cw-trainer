#include "test_util.h"
#include "cw_decoder.h"
#include <string.h>

/* 用理想时序把 pattern 敲进解码器，返回 t 结束时刻 */
static uint32_t send_pattern(cw_decoder_t *d, const char *pat, uint32_t t, uint16_t dit)
{
    for (const char *p = pat; *p; p++) {
        cw_decoder_key(d, true, t);
        t += (*p == '.') ? dit : dit * 3;
        cw_decoder_key(d, false, t);
        t += dit;
    }
    return t;
}

static int collect(cw_decoder_t *d, uint32_t from, uint32_t to, char *buf, int cap)
{
    int n = 0; char c;
    for (uint32_t t = from; t <= to; t += 5) {
        if (cw_decoder_poll(d, t, &c) && n < cap - 1) buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

TEST(decodes_single_char_after_gap)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = send_pattern(&d, "-.-", 0, 60);   /* K */
    char buf[8];
    collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "K ");
}

TEST(pattern_visible_while_pending)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    cw_decoder_key(&d, true, 0);
    cw_decoder_key(&d, false, 60);
    cw_decoder_key(&d, true, 120);
    cw_decoder_key(&d, false, 300);
    ASSERT_EQ_STR(cw_decoder_pattern(&d), ".-");
}

TEST(decodes_word_with_char_gaps)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = 0;
    const char *word = "CQ";
    char buf[16]; int n = 0;
    for (const char *p = word; *p; p++) {
        const char *pat = (*p == 'C') ? "-.-." : "--.-";
        t = send_pattern(&d, pat, t, 60);
        t += 120;  /* 字符间总 3 dit：send_pattern 末尾已含 1 dit */
        n += collect(&d, t - 120, t, buf + n, (int)sizeof buf - n);
    }
    n += collect(&d, t, t + 1000, buf + n, (int)sizeof buf - n);
    ASSERT_EQ_STR(buf, "CQ ");
}

TEST(tolerates_20_percent_jitter)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    /* S = ... 用 72ms 的点（+20%）；O = --- 用 144ms 的划（-20%） */
    uint32_t t = 0;
    for (int i = 0; i < 3; i++) { cw_decoder_key(&d, true, t); t += 72; cw_decoder_key(&d, false, t); t += 60; }
    /* 自适应后 dit_ref≈66：字符判定阈值≈132ms，词间阈值≈330ms；窗口取 200ms 只出字符不出空格 */
    t += 200;
    char buf[8]; collect(&d, t - 200, t, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "S");
    for (int i = 0; i < 3; i++) { cw_decoder_key(&d, true, t); t += 144; cw_decoder_key(&d, false, t); t += 60; }
    collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "O ");
}

TEST(unknown_pattern_gives_question_mark)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = send_pattern(&d, "........", 0, 60);
    char buf[8]; collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "? ");
}

TEST(upward_runaway_blocked)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = 0;
    /* 两个临界过长样本：119ms（119<120，按点判定）随后 147ms。
     * 若没有护栏，第一个样本会把 ref 从 60 拉到 74，随后 147<148 又被判成点，
     * ref 冲到 92，从此真正的划（180ms）也会被误判成点，一路失控。
     * 护栏应让这两个样本都不进入平均。 */
    cw_decoder_key(&d, true, t); t += 119; cw_decoder_key(&d, false, t); t += 60;
    cw_decoder_key(&d, true, t); t += 147; cw_decoder_key(&d, false, t); t += 60;
    char scratch[8];
    t += 1000;  /* 冲刷掉上面产生的（无论内容如何）字符/词间隔，不关心其内容 */
    collect(&d, t - 1000, t, scratch, sizeof scratch);
    /* 干净的 60ms 时序发送 K；若 ref 已失控上冲，180ms 的划会被误判成点 */
    t = send_pattern(&d, "-.-", t, 60);
    char buf[8]; collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "K ");
}

TEST(downward_crash_blocked)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = 0;
    /* 四个零长度样本（接触弹跳/单个 tick 的误触）都远低于合理点长。
     * 若没有护栏，逐次 adapt 会把 ref 一路拽到下限 20（60->45->33->24->20），
     * 此后真正的 60ms 点也会被误判成划。护栏应让这些样本都不进入平均，
     * ref 全程停在 60。 */
    for (int i = 0; i < 4; i++) {
        cw_decoder_key(&d, true, t); cw_decoder_key(&d, false, t); t += 60;
    }
    char scratch[8];
    t += 1000;
    collect(&d, t - 1000, t, scratch, sizeof scratch);
    /* 干净的 60ms 时序发送 S；若 ref 已失控下探，60ms 的点会被误判成划 */
    t = send_pattern(&d, "...", t, 60);
    char buf[8]; collect(&d, t, t + 1000, buf, sizeof buf);
    ASSERT_EQ_STR(buf, "S ");
}

TEST(genuine_speed_change_still_tracked)
{
    cw_decoder_t d; cw_decoder_init(&d, 60);
    uint32_t t = 0;
    char buf[16]; int n = 0;
    /* S I H：全部由点组成，40ms 真实点长下应逐步收敛（60->55->51->48->46->44->...->40），
     * 证明护栏只挡失控样本，不会冻结对真实变速的跟踪。 */
    const char *pats[] = { "...", "..", "...." };
    for (int i = 0; i < 3; i++) {
        t = send_pattern(&d, pats[i], t, 40);
        t += 500;  /* 远大于任意阶段的 5x ref（最大 300ms），保证冲刷字符与词间隔 */
        n += collect(&d, t - 500, t, buf + n, (int)sizeof buf - n);
    }
    ASSERT_EQ_STR(buf, "S I H ");
}

int main(void)
{
    RUN(decodes_single_char_after_gap);
    RUN(pattern_visible_while_pending);
    RUN(decodes_word_with_char_gaps);
    RUN(tolerates_20_percent_jitter);
    RUN(unknown_pattern_gives_question_mark);
    RUN(upward_runaway_blocked);
    RUN(downward_crash_blocked);
    RUN(genuine_speed_change_still_tracked);
    TEST_MAIN_END();
}
