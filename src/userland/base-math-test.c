#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { \
  printf("base-math-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
  return 1; \
} } while (0)

static int close_to(double value, double expected) {
  double difference = value - expected;
  if (difference < 0) difference = -difference;
  double magnitude = expected < 0 ? -expected : expected;
  return difference <= 3 * DBL_EPSILON * magnitude;
}

int main(void) {
  uint32_t saved_mxcsr;
  uint16_t saved_x87;
  __asm__ volatile("stmxcsr %0" : "=m"(saved_mxcsr));
  __asm__ volatile("fnstcw %0" : "=m"(saved_x87));
  CHECK(floor(1.25) == 1 && floor(-1.25) == -2);
  CHECK(ceil(1.25) == 2 && ceil(-1.25) == -1);
  CHECK(signbit(floor(-0.0)) && signbit(ceil(-0.0)));
  CHECK(floor(0x1p-1074) == 0 && floor(-0x1p-1074) == -1);
  CHECK(ceil(0x1p-1074) == 1 && signbit(ceil(-0x1p-1074)));
  CHECK(floor(0x1p52) == 0x1p52 && ceil(0x1p52) == 0x1p52);
  CHECK(isinf(floor(INFINITY)) && signbit(floor(-INFINITY)));
  CHECK(isinf(ceil(INFINITY)) && signbit(ceil(-INFINITY)));
  CHECK(isnan(floor(NAN)) && isnan(ceil(NAN)));
  CHECK(ceilf(1.25f) == 2 && ceilf(-1.25f) == -1);
  CHECK(ceilf(0.0f) == 0 && signbit(ceilf(-0.0f)));
  CHECK(signbit(ceilf(-0.125f)) && ceilf(0x1p-149f) == 1);
  CHECK(isinf(ceilf(INFINITY)) && isnan(ceilf(NAN)));
  CHECK(round(0.5) == 1 && round(-0.5) == -1);
  CHECK(round(1.5) == 2 && round(-1.5) == -2);
  CHECK(round(0x1.fffffffffffffp-2) == 0);
  CHECK(signbit(round(-0.125)) && round(0x1p52) == 0x1p52);
  CHECK(isinf(round(INFINITY)) && isnan(round(NAN)));
  int exponent;
  CHECK(frexp(8, &exponent) == 0.5 && exponent == 4);
  CHECK(frexp(-DBL_MIN, &exponent) == -0.5 && exponent == -1021);
  CHECK(frexp(0x1p-1074, &exponent) == 0.5 && exponent == -1073);
  CHECK(signbit(frexp(-0.0, &exponent)) && exponent == 0);
  CHECK(isinf(frexp(INFINITY, &exponent)) && isnan(frexp(NAN, &exponent)));
  CHECK(ldexp(0.5, 4) == 8 && signbit(ldexp(-0.0, 100)));
  CHECK(ldexpl(0.5L, 4) == 8 && signbit(ldexpl(-0.0L, 100)));
  CHECK(ldexpl(LDBL_MIN, -1) == LDBL_MIN / 2);
  CHECK(isinf(ldexpl(INFINITY, 100)) && isnan(ldexpl(NAN, 100)));
  errno = 0;
  CHECK(isinf(ldexpl(1, 16384)) && errno == ERANGE);
  errno = 0;
  CHECK(isinf(ldexp(1, 1024)) && errno == ERANGE);
  errno = 0;
  CHECK(ldexp(1, -1075) == 0 && errno == ERANGE);
  errno = 0;
  CHECK(exp(0) == 1 && exp(-INFINITY) == 0 && errno == 0);
  CHECK(isinf(exp(INFINITY)) && isnan(exp(NAN)) && errno == 0);
  CHECK(close_to(exp(1), 0x1.5bf0a8b145769p+1));
  CHECK(close_to(exp(-1), 0x1.78b56362cef38p-2));
  CHECK(close_to(exp(10), 0x1.5829dcf950560p+14));
  errno = 0;
  CHECK(isinf(exp(710)) && errno == ERANGE);
  errno = 0;
  CHECK(exp(-746) == 0 && errno == ERANGE);
  errno = 0;
  CHECK(exp(-744) > 0 && exp(-744) < DBL_MIN && errno == ERANGE);
  errno = 0;
  CHECK(log(1) == 0 && !signbit(log(1)) && errno == 0);
  CHECK(close_to(log(2), 0x1.62e42fefa39efp-1));
  CHECK(close_to(log(10), 0x1.26bb1bbb55516p+1));
  CHECK(close_to(log(0x1p-1074), -0x1.74385446d71c3p+9));
  CHECK(isinf(log(INFINITY)) && isnan(log(NAN)) && errno == 0);
  errno = 0;
  CHECK(isinf(log(-0.0)) && signbit(log(-0.0)) && errno == ERANGE);
  errno = 0;
  CHECK(isnan(log(-1)) && errno == EDOM);
  uint32_t mode = (saved_mxcsr & ~0x603fu) | 0x2000u;
  uint16_t x87_mode = (saved_x87 & ~0x0c00u) | 0x0400u;
  __asm__ volatile("ldmxcsr %0" :: "m"(mode));
  __asm__ volatile("fldcw %0" :: "m"(x87_mode));
  CHECK(floor(-1.25) == -2 && ceil(-1.25) == -1);
  errno = 0;
  CHECK(exp(710) == DBL_MAX && errno == ERANGE);
  errno = 0;
  CHECK(ldexp(1, 1024) == DBL_MAX && errno == ERANGE);
  mode = saved_mxcsr & ~0x603fu;
  x87_mode = saved_x87 & ~0x0c00u;
  __asm__ volatile("ldmxcsr %0" :: "m"(mode));
  __asm__ volatile("fldcw %0" :: "m"(x87_mode));
  errno = 0;
  CHECK(isnan(log(-1)) && errno == EDOM);
  __asm__ volatile("stmxcsr %0" : "=m"(mode));
  CHECK(mode & 1u);
  mode = saved_mxcsr & ~0x603fu;
  __asm__ volatile("ldmxcsr %0" :: "m"(mode));
  errno = 0;
  CHECK(isinf(log(0)) && errno == ERANGE);
  __asm__ volatile("stmxcsr %0" : "=m"(mode));
  CHECK(mode & 4u);
  __asm__ volatile("ldmxcsr %0" :: "m"(saved_mxcsr));
  __asm__ volatile("fldcw %0" :: "m"(saved_x87));
  puts("base-math-test: PASS");
  return 0;
}
