/* musl floating-point exceptional operations, with native POSIX errno.
 * MIT license: see COPYRIGHT.musl. */
#include <errno.h>
#include "libm.h"

double __math_oflow(uint32_t sign) {
  errno = ERANGE;
  double y = 0x1p769;
  return eval_as_double(fp_barrier(sign ? -y : y) * y);
}
double __math_uflow(uint32_t sign) {
  errno = ERANGE;
  double y = 0x1p-767;
  return eval_as_double(fp_barrier(sign ? -y : y) * y);
}
double __math_divzero(uint32_t sign) {
  errno = ERANGE;
  return fp_barrier(sign ? -1.0 : 1.0) / 0.0;
}
double __math_invalid(double x) {
  if (!isnan(x)) errno = EDOM;
  return (x - x) / (x - x);
}
