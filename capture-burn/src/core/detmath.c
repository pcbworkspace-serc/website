#include "detmath.h"
#include <stdint.h>
#include <string.h>

/* sqrt is correctly rounded by IEEE-754; the builtin compiles to a single
 * instruction (sqrtsd / fsqrt / vsqrt.f64) with -fno-math-errno. */
double dm_sqrt(double x) { return __builtin_sqrt(x); }
double dm_fabs(double x) { return x < 0 ? -x : x; }

double dm_floor(double x)
{
    /* Exact: doubles >= 2^52 are already integers. */
    if (!(dm_fabs(x) < 4503599627370496.0)) return x;
    double t = (double)(int64_t)x;
    return (t > x) ? t - 1.0 : t;
}

double dm_clamp(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* Cody-Waite split of pi/2 so range reduction stays accurate. */
static const double PIO2_HI = 1.57079632673412561417e+00;
static const double PIO2_LO = 6.07710050650619224932e-11;

static double sin_kernel(double x)
{
    double z = x * x;
    return x + x * z * (-1.0 / 6.0 + z * (1.0 / 120.0 + z * (-1.0 / 5040.0 +
           z * (1.0 / 362880.0 + z * (-1.0 / 39916800.0 + z * (1.0 / 6227020800.0 +
           z * (-1.0 / 1307674368000.0)))))));
}

static double cos_kernel(double x)
{
    double z = x * x;
    return 1.0 + z * (-0.5 + z * (1.0 / 24.0 + z * (-1.0 / 720.0 + z * (1.0 / 40320.0 +
           z * (-1.0 / 3628800.0 + z * (1.0 / 479001600.0 + z * (-1.0 / 87178291200.0)))))));
}

/* Reduce x to r in [-pi/4, pi/4] and quadrant q. */
static double reduce(double x, int *q)
{
    double k = dm_floor(x * (2.0 / DM_PI) + 0.5);
    double r = (x - k * PIO2_HI) - k * PIO2_LO;
    int64_t ki = (int64_t)k;
    *q = (int)(ki & 3);
    return r;
}

double dm_sin(double x)
{
    int q;
    double r = reduce(x, &q);
    switch (q) {
    case 0: return sin_kernel(r);
    case 1: return cos_kernel(r);
    case 2: return -sin_kernel(r);
    default: return -cos_kernel(r);
    }
}

double dm_cos(double x)
{
    int q;
    double r = reduce(x, &q);
    switch (q) {
    case 0: return cos_kernel(r);
    case 1: return -sin_kernel(r);
    case 2: return -cos_kernel(r);
    default: return sin_kernel(r);
    }
}

double dm_wrap_angle(double a)
{
    return a - DM_TAU * dm_floor((a + DM_PI) / DM_TAU);
}

double dm_atan(double x)
{
    int neg = x < 0;
    if (neg) x = -x;
    int inv = x > 1.0;
    if (inv) x = 1.0 / x;
    /* Two half-angle reductions: atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))).
     * After two, |x| <= tan(pi/16) ~ 0.199. */
    x = x / (1.0 + dm_sqrt(1.0 + x * x));
    x = x / (1.0 + dm_sqrt(1.0 + x * x));
    double z = x * x, term = x, sum = x;
    for (int n = 3; n <= 25; n += 2) {
        term *= -z;
        sum += term / (double)n;
    }
    double r = 4.0 * sum;
    if (inv) r = DM_PI / 2.0 - r;
    return neg ? -r : r;
}

double dm_atan2(double y, double x)
{
    if (x > 0) return dm_atan(y / x);
    if (x < 0) return (y >= 0) ? dm_atan(y / x) + DM_PI : dm_atan(y / x) - DM_PI;
    if (y > 0) return DM_PI / 2.0;
    if (y < 0) return -DM_PI / 2.0;
    return 0.0;
}

/* Exact power-of-two scaling via exponent bits (no libm ldexp). */
static double scale2(double x, int k)
{
    /* Split to keep each factor a normal double. */
    while (k > 1000) { x *= 0x1p1000; k -= 1000; }
    while (k < -1000) { x *= 0x1p-1000; k += 1000; }
    uint64_t bits = (uint64_t)(int64_t)(k + 1023) << 52;
    double f;
    memcpy(&f, &bits, sizeof f);
    return x * f;
}

static const double LN2_HI = 6.93147180369123816490e-01;
static const double LN2_LO = 1.90821492927058770002e-10;

double dm_exp(double x)
{
    if (x > 709.0) x = 709.0;
    if (x < -708.0) return 0.0;
    double k = dm_floor(x * 1.44269504088896340736 + 0.5);
    double r = (x - k * LN2_HI) - k * LN2_LO; /* |r| <= ln2/2 */
    double term = 1.0, sum = 1.0;
    for (int n = 1; n <= 16; n++) {
        term *= r / (double)n;
        sum += term;
    }
    return scale2(sum, (int)k);
}

double dm_log(double x)
{
    if (!(x > 0)) return -1e308;
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    int e = (int)((bits >> 52) & 0x7ff);
    if (e == 0) { /* subnormal: renormalise */
        x *= 0x1p54;
        memcpy(&bits, &x, sizeof bits);
        e = (int)((bits >> 52) & 0x7ff) - 54;
    }
    e -= 1023;
    bits = (bits & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL; /* m in [1,2) */
    double m;
    memcpy(&m, &bits, sizeof m);
    if (m > 1.41421356237309504880) { m *= 0.5; e += 1; }
    /* log(m) = 2 atanh(s), s = (m-1)/(m+1), |s| <= 0.1716 */
    double s = (m - 1.0) / (m + 1.0);
    double z = s * s, term = s, sum = s;
    for (int n = 3; n <= 31; n += 2) {
        term *= z;
        sum += term / (double)n;
    }
    double ed = (double)e;
    return ed * LN2_HI + (2.0 * sum + ed * LN2_LO);
}

double dm_pow(double x, double y) { return dm_exp(y * dm_log(x)); }

double dm_cbrt(double x)
{
    double r = dm_exp(dm_log(x) / 3.0);
    r = r - (r * r * r - x) / (3.0 * r * r); /* one Newton step */
    return r;
}
