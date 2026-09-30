#ifndef _MATH_H
#define _MATH_H
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define HUGE_VAL (__builtin_inf())
#define HUGE_VALF (__builtin_inff())
#define HUGE_VALL (__builtin_infl())
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(a, b) __builtin_isgreater((a), (b))
#define isgreaterequal(a, b) __builtin_isgreaterequal((a), (b))
#define isless(a, b) __builtin_isless((a), (b))
#define islessequal(a, b) __builtin_islessequal((a), (b))
#define islessgreater(a, b) __builtin_islessgreater((a), (b))
#define isunordered(a, b) __builtin_isunordered((a), (b))
#ifdef __cplusplus
extern "C" {
#endif
double scalbn(double x, int n);
long double scalbnl(long double x, int n);
long double frexpl(long double x, int *exponent);
long double fabsl(long double x);
long double copysignl(long double x, long double y);
long double fmodl(long double x, long double y);
#ifdef __cplusplus
}
#endif
#endif
