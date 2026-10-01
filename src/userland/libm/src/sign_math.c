/* Operations C99 sur le bit de signe IEEE-754 binary32/binary64. */
#include <stdint.h>
#include <math.h>

double fabs(double x) {
  union { double f; uint64_t u; } v = {x};
  v.u &= UINT64_C(0x7fffffffffffffff);
  return v.f;
}

float fabsf(float x) {
  union { float f; uint32_t u; } v = {x};
  v.u &= UINT32_C(0x7fffffff);
  return v.f;
}

double copysign(double x, double y) {
  union { double f; uint64_t u; } vx = {x}, vy = {y};
  vx.u = (vx.u & UINT64_C(0x7fffffffffffffff)) |
         (vy.u & UINT64_C(0x8000000000000000));
  return vx.f;
}

float copysignf(float x, float y) {
  union { float f; uint32_t u; } vx = {x}, vy = {y};
  vx.u = (vx.u & UINT32_C(0x7fffffff)) | (vy.u & UINT32_C(0x80000000));
  return vx.f;
}
