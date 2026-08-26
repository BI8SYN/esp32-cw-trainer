#ifndef RNG_H
#define RNG_H
#include <stdint.h>

typedef struct { uint32_t s; } rng_t;

static inline void rng_seed(rng_t *r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }

static inline uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}

static inline uint32_t rng_below(rng_t *r, uint32_t n) { return n ? rng_next(r) % n : 0; }

#endif
