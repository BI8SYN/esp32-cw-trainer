#include "tone_seq.h"

/*
 * 提示音刻意做成「不是莫尔斯」，靠两条：
 *
 *   1. **20ms 短于任何现实中的点。** 40 WPM 的点是 30ms，而 40 WPM 已经是很快
 *      的手速了。20ms 落在莫尔斯的时长空间之外。
 *   2. **序列内频率在变，而莫尔斯从不变频。** 一段升调或降调，耳朵一听就知道
 *      这不是电码。
 *
 * 这两条加起来，提示音和这台机器发出的「教材」分属两个世界，不会互相污染。
 * 反过来说，任何单频、且时长在点划量级的提示音都是不合格的——那就是在教人
 * 一个语义错误的电码。
 */

#define BLIP_MS   20
#define GAP_MS    40
/*
 * 异常音也必须变频。
 *
 * 第一版是单频 120ms 的低音，被自己写的测试当场否掉了——**30 WPM 的划正好是
 * 120ms**，那个声音会被听成字母 T。它既不短于点、序列内又不变频，spec 里立的
 * 两条依据一条都不占，而我还是把它写进了表里。
 *
 * 改成降调双音：两段都在侧音之下，且频率在变。低、长、往下掉——听感上是
 * 「出事了」，而不是任何一个电码。
 */
#define FAULT_MS  90
#define FAULT_GAP 30

/*
 * 上限 4000：侧音可设到 1200Hz，×4 就是 4800——远低于 32kHz 的奈奎斯特上限，
 * 但高频刺耳且小喇叭响应差。
 *
 * 下限 300：和 tone_gen_set_freq() 给侧音定的下限一致。那个 300 说明设计者
 * 已经判断过这块小喇叭的实际下沿，提示音没理由比它更低——**低到发不出声的
 * 提示音等于没有提示音**。
 */
#define HZ_MIN    300
#define HZ_MAX    4000

static uint16_t mul(uint16_t base, int num, int den)
{
    long v = (long)base * num / den;
    if (v < HZ_MIN) v = HZ_MIN;
    if (v > HZ_MAX) v = HZ_MAX;
    return (uint16_t)v;
}

static int emit(tone_step_t *out, int max, int n, uint16_t hz, uint16_t ms)
{
    if (n >= max) return n;
    out[n].hz = hz;
    out[n].ms = ms;
    return n + 1;
}

int tone_seq_build(tone_seq_id_t id, uint16_t base_hz, tone_step_t *out, int max)
{
    if (!out || max <= 0) return 0;
    int n = 0;
    switch (id) {
    case TONE_SEQ_TAP:
        n = emit(out, max, n, mul(base_hz, 2, 1), BLIP_MS);
        break;
    case TONE_SEQ_OK:            /* 升调：对了 */
        n = emit(out, max, n, mul(base_hz, 2, 1), BLIP_MS);
        n = emit(out, max, n, 0,                  GAP_MS);
        n = emit(out, max, n, mul(base_hz, 3, 1), BLIP_MS);
        break;
    case TONE_SEQ_MISS:          /* 降调：错了。和 OK 互为倒序，不用记也分得清 */
        n = emit(out, max, n, mul(base_hz, 3, 1), BLIP_MS);
        n = emit(out, max, n, 0,                  GAP_MS);
        n = emit(out, max, n, mul(base_hz, 2, 1), BLIP_MS);
        break;
    case TONE_SEQ_UNLOCK:        /* 三级升调：比判对更进一步 */
        n = emit(out, max, n, mul(base_hz, 2, 1), BLIP_MS);
        n = emit(out, max, n, 0,                  GAP_MS);
        n = emit(out, max, n, mul(base_hz, 3, 1), BLIP_MS);
        n = emit(out, max, n, 0,                  GAP_MS);
        n = emit(out, max, n, mul(base_hz, 4, 1), BLIP_MS);
        break;
    case TONE_SEQ_FAULT:
        /*
         * 降一个五度，而不是「压到侧音以下」。
         *
         * 本来想让它又低又长，但侧音最低能设到 300Hz，再往下就被 HZ_MIN 夹住
         * ——两段会夹成同一个频率，**降调塌成单频，又变回一个能被听成划的音**。
         * 从 ×1.5 降到 ×0.75 则在任何侧音设定下都保持着落差。
         */
        n = emit(out, max, n, mul(base_hz, 3, 2), FAULT_MS);
        n = emit(out, max, n, 0,                  FAULT_GAP);
        n = emit(out, max, n, mul(base_hz, 3, 4), FAULT_MS);
        break;
    }
    return n;
}
