#include "key_merge.h"

void key_merge(const bool touch[2], const bool ext[2], const bool armed[2],
               bool swap, key_merge_out_t *out)
{
    for (int i = 0; i < 2; i++) {
        /* 门闩只掐外接那一路。写成 touch[i] || (ext && armed) 而不是
           (touch[i] || ext[i]) && armed[i]：后者会让一根坏线连带把同侧的触摸块
           一起废掉，一根线的故障就变成整台机器不能用。 */
        out->in[i] = touch[i] || (ext[i] && armed[i]);
    }
    out->dit = out->in[swap ? 1 : 0];
    out->dah = out->in[swap ? 0 : 1];
}
