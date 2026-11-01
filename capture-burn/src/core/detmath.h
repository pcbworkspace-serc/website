/* Deterministic math.
 *
 * libm's sin/cos/exp/log/pow are not guaranteed to round identically on
 * glibc x86_64, glibc aarch64 and musl/armhf. These versions use only
 * + - * / and sqrt (all correctly rounded by IEEE-754) plus exact bit
 * operations (floor, frexp, ldexp), so a given input produces the same
 * bits on every platform as long as the compiler does not contract a*b+c
 * into FMA (-ffp-contract=off, enforced in CMakeLists.txt).
 *
 * Accuracy is ~1e-15 relative over the ranges the simulation uses; the
 * test suite checks it against libm.
 */
#ifndef CB_DETMATH_H
#define CB_DETMATH_H

#define DM_PI 3.14159265358979323846
#define DM_TAU 6.28318530717958647692

double dm_sqrt(double x);
double dm_floor(double x);
double dm_fabs(double x);
double dm_sin(double x);
double dm_cos(double x);
double dm_atan(double x);
double dm_atan2(double y, double x);
double dm_exp(double x);
double dm_log(double x);
double dm_pow(double x, double y); /* x > 0 */
double dm_cbrt(double x);          /* x > 0 */
double dm_wrap_angle(double a);    /* to [-pi, pi) */
double dm_clamp(double x, double lo, double hi);

#endif
