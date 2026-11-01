#include "rng.h"

uint32_t rng_u32(Rng *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void rng_seed(Rng *r, uint64_t seed, uint64_t stream)
{
    r->state = 0u;
    r->inc = (stream << 1u) | 1u;
    rng_u32(r);
    r->state += seed;
    rng_u32(r);
}

double rng_unit(Rng *r)
{
    uint64_t hi = rng_u32(r) >> 5;  /* 27 bits */
    uint64_t lo = rng_u32(r) >> 6;  /* 26 bits */
    return (double)((hi << 26) | lo) * (1.0 / 9007199254740992.0);
}

double rng_range(Rng *r, double lo, double hi) { return lo + (hi - lo) * rng_unit(r); }

uint32_t rng_below(Rng *r, uint32_t n)
{
    uint32_t threshold = (uint32_t)(-n) % n;
    for (;;) {
        uint32_t x = rng_u32(r);
        if (x >= threshold) return x % n;
    }
}
