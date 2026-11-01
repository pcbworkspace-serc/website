/* PCG32 (O'Neill, pcg-random.org): small, fast, and specified bit-exactly,
 * so every platform draws the same sequence from the same seed. */
#ifndef CB_RNG_H
#define CB_RNG_H
#include <stdint.h>

typedef struct { uint64_t state, inc; } Rng;

void rng_seed(Rng *r, uint64_t seed, uint64_t stream);
uint32_t rng_u32(Rng *r);
double rng_unit(Rng *r);                    /* [0, 1) with 53 bits */
double rng_range(Rng *r, double lo, double hi);
uint32_t rng_below(Rng *r, uint32_t n);     /* [0, n) unbiased */

#endif
