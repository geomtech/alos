/* Narrow adaptation of musl's internal libm helpers (MIT, COPYRIGHT.musl).
 * Only the native x86-64 binary32/binary64 algorithms are imported here. */
#ifndef _ALOS_MUSL_LIBM_H
#define _ALOS_MUSL_LIBM_H
#include <float.h>
#include <math.h>
#include <stdint.h>

#define WANT_ROUNDING 1
#define TOINT_INTRINSICS 0
#define predict_false(x) __builtin_expect(!!(x), 0)
#define asuint64(f) ((union { double value; uint64_t bits; }){f}).bits
#define asdouble(i) ((union { uint64_t bits; double value; }){i}).value

static inline double eval_as_double(double x) { return x; }
static inline double fp_barrier(double x) {
  volatile double y = x;
  return y;
}
static inline void fp_force_eval(double x) {
  volatile double y = x;
  (void)y;
}
#define FORCE_EVAL(x) do { \
  volatile __typeof__(x) result = (x); \
  (void)result; \
} while (0)

double __math_oflow(uint32_t sign);
double __math_uflow(uint32_t sign);
double __math_divzero(uint32_t sign);
double __math_invalid(double x);
#endif
