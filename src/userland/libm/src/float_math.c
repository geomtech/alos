/* x86-64 80-bit long double helpers adapted from musl (MIT license).
 * See ports/chromium/MUSL-LICENSE for attribution and license terms. */
#include <stdint.h>
#include <float.h>
#include <math.h>

#if LDBL_MANT_DIG != 64 || LDBL_MAX_EXP != 16384 || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Unsupported ALOS long double ABI
#endif

union ldshape {
  long double f;
  struct { uint64_t m; uint16_t se; } i;
};

long double fabsl(long double x) {
  union ldshape u = {x};
  u.i.se &= 0x7fff;
  return u.f;
}

long double copysignl(long double x, long double y) {
  union ldshape ux = {x}, uy = {y};
  ux.i.se = (ux.i.se & 0x7fff) | (uy.i.se & 0x8000);
  return ux.f;
}

long double scalbnl(long double x, int n) {
  union ldshape u;
  if (n > 16383) {
    x *= 0x1p16383L;
    n -= 16383;
    if (n > 16383) {
      x *= 0x1p16383L;
      n -= 16383;
      if (n > 16383) n = 16383;
    }
  } else if (n < -16382) {
    x *= 0x1p-16382L * 0x1p113L;
    n += 16382 - 113;
    if (n < -16382) {
      x *= 0x1p-16382L * 0x1p113L;
      n += 16382 - 113;
      if (n < -16382) n = -16382;
    }
  }
  u.f = 1.0L;
  u.i.se = 0x3fff + n;
  return x * u.f;
}

double scalbn(double x, int n) { return (double)scalbnl((long double)x, n); }

long double fmodl(long double x, long double y) {
  union ldshape ux = {x}, uy = {y};
  int ex = ux.i.se & 0x7fff;
  int ey = uy.i.se & 0x7fff;
  int sx = ux.i.se & 0x8000;
  if (y == 0 || isnan(y) || ex == 0x7fff) return (x*y)/(x*y);
  ux.i.se = ex;
  uy.i.se = ey;
  if (ux.f <= uy.f) {
    if (ux.f == uy.f) return 0*x;
    return x;
  }
  if (!ex) { ux.f *= 0x1p120f; ex = ux.i.se - 120; }
  if (!ey) { uy.f *= 0x1p120f; ey = uy.i.se - 120; }
  uint64_t i, mx = ux.i.m, my = uy.i.m;
  for (; ex > ey; --ex) {
    i = mx - my;
    if (mx >= my) {
      if (!i) return 0*x;
      mx = 2*i;
    } else if (2*mx < mx) mx = 2*mx - my;
    else mx = 2*mx;
  }
  i = mx - my;
  if (mx >= my) {
    if (!i) return 0*x;
    mx = i;
  }
  while (!(mx >> 63)) { mx *= 2; --ex; }
  ux.i.m = mx;
  if (ex <= 0) {
    ux.i.se = (ex + 120) | sx;
    ux.f *= 0x1p-120f;
  } else ux.i.se = ex | sx;
  return ux.f;
}
