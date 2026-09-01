#ifndef TONE_SEQ_H
#define TONE_SEQ_H
#include <stdint.h>

/*
 * 提示音的音符序列。纯数据 + 纯函数，不依赖 ESP-IDF，所以能在主机上测。
 *
 * 为什么放在 core/ 而不是和 tone_gen 放一起：tone_gen.h 要 include esp_err.h，
 * 一旦把这个结构体定在那里，主机测试就再也碰不到它了。而「给定事件和侧音频率，
 * 应该产出什么频率什么时长」恰恰是这个功能里唯一值得测、也唯一测得了的部分
 * ——仲裁行为只有耳朵能验。
 */

typedef struct {
    uint16_t hz;    /* 0 表示静音间隔 */
    uint16_t ms;
} tone_step_t;

#define TONE_SEQ_MAX 8

typedef enum {
    TONE_SEQ_TAP,       /* 导航点击 */
    TONE_SEQ_OK,        /* 判对 */
    TONE_SEQ_MISS,      /* 判错 */
    TONE_SEQ_UNLOCK,    /* 课次解锁 */
    TONE_SEQ_FAULT,     /* 异常 */
} tone_seq_id_t;

/*
 * 按事件和当前侧音频率生成序列，返回步数（<= TONE_SEQ_MAX）。
 *
 * 频率取侧音的倍数而不是写死：用户能在设置里改音调（400~1200Hz），写死的
 * 提示音在某些设定下会和侧音同频、听起来就是一段莫尔斯。取倍数则相对关系
 * 恒定，永远差着一个八度以上。
 */
int tone_seq_build(tone_seq_id_t id, uint16_t base_hz, tone_step_t *out, int max);

#endif
