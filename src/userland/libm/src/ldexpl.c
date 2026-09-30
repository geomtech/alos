#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>

long double ldexpl(long double x, int n) {
  long double result = scalbnl(x, n);
  if (isfinite(x) && x != 0) {
    int exponent;
    frexpl(x, &exponent);
    int64_t scaled_exponent = (int64_t)exponent + n;
    if (scaled_exponent > LDBL_MAX_EXP || scaled_exponent < LDBL_MIN_EXP)
      errno = ERANGE;
  }
  return result;
}
