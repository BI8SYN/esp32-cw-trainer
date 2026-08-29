#ifndef KEY_MERGE_H
#define KEY_MERGE_H
#include <stdbool.h>

/*
 * 触摸桨与外接口的合并。纯函数，没有副作用，主机侧单测可以直接跑。
 *
 * 抽出来是因为「并联 × 门闩 × 左右对调」三者的交互是最容易写错的地方，而它在
 * key_input.c 里和 GPIO 读取、FreeRTOS 任务缠在一起，那样一条测试都写不了。
 */

typedef struct {
    bool dit, dah;   /* 送给 keyer 的两路，已应用左右对调 */
    bool in[2];      /* 屏幕上第 i 块（0=左，1=右）的按下状态，对调**之前** */
} key_merge_out_t;

/*
 * touch[i]  触摸桨第 i 块当前是否按下
 * ext[i]    外接线 i 去抖后的状态（true = 按下）
 * armed[i]  门闩：false 表示这根线还没被观察到松开过，它说什么都不算
 * swap      桨键左右对调
 *
 * out->in[] 取在对调之前，所以它恒等于「屏幕上第 i 块」的状态，与对调开关无关
 * ——UI 高亮要的正是这个量。若让 in[] 也跟着对调，开了对调之后按左块会去高亮
 * 右块。
 */
void key_merge(const bool touch[2], const bool ext[2], const bool armed[2],
               bool swap, key_merge_out_t *out);

#endif
