/* Adapted from musl 1.2.6 atan2l.c, atanl.c and __invtrigl.c.
 * Converted to long double by David Schultz <das@FreeBSD.ORG>.
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunSoft/SunPro, Sun Microsystems, Inc. businesses.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 * See src/userland/libc/COPYRIGHT.musl.
 */
#include <float.h>
#include <math.h>
#include <stdint.h>

#if LDBL_MANT_DIG != 64 || LDBL_MAX_EXP != 16384 || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Unsupported ALOS long double ABI
#endif
union atan_ldshape {
  long double f;
  struct { uint64_t m; uint16_t se; } i;
};
static const long double pio2_hi = 1.57079632679489661926L;
static const long double pio2_lo = -2.50827880633416601173e-20L;

static long double reduced_atan(long double x) {
  static const long double hi[] = {
    4.63647609000806116202e-01L, 7.85398163397448309628e-01L,
    9.82793723247329067960e-01L, 1.57079632679489661926e+00L
  };
  static const long double lo[] = {
    1.18469937025062860669e-20L, -1.25413940316708300586e-20L,
    2.55232234165405176172e-20L, -2.50827880633416601173e-20L
  };
  static const long double a[] = {
    3.33333333333333333017e-01L, -1.99999999999999632011e-01L,
    1.42857142857046531280e-01L, -1.11111111100562372733e-01L,
    9.09090902935647302252e-02L, -7.69230552476207730353e-02L,
    6.66661718042406260546e-02L, -5.88158892835030888692e-02L,
    5.25499891539726639379e-02L, -4.70119845393155721494e-02L,
    4.03539201366454414072e-02L, -2.91303858419364158725e-02L,
    1.24822046299269234080e-02L
  };
  union atan_ldshape u = {.f = x};
  unsigned e = u.i.se & 0x7fff, sign = u.i.se >> 15;
  if (e >= 0x3fff + LDBL_MANT_DIG + 1) {
    if (isnan(x)) return x;
    return sign ? -hi[3] : hi[3];
  }
  unsigned expman = (e << 8) | ((u.i.m >> 55) & 0xff);
  int id;
  if (expman < ((0x3fff-2) << 8)+0xc0) {
    if (e < 0x3fff-(LDBL_MANT_DIG+1)/2) {
      if (!e) { volatile float tiny = (float)x; (void)tiny; }
      return x;
    }
    id = -1;
  } else {
    x = fabsl(x);
    if (expman < (0x3fff << 8)+0x30) {
      if (expman < ((0x3fff-1) << 8)+0x60) { id = 0; x = (2*x-1)/(2+x); }
      else { id = 1; x = (x-1)/(x+1); }
    } else {
      if (expman < ((0x3fff+1) << 8)+0x38) { id = 2; x = (x-1.5L)/(1+1.5L*x); }
      else { id = 3; x = -1/x; }
    }
  }
  long double z = x*x, w = z*z;
  long double even = a[0]+w*(a[2]+w*(a[4]+w*(a[6]+w*(a[8]+w*(a[10]+w*a[12])))));
  long double odd = a[1]+w*(a[3]+w*(a[5]+w*(a[7]+w*(a[9]+w*a[11]))));
  long double sum = z*even+w*odd;
  if (id < 0) return x-x*sum;
  z = hi[id]-((x*sum-lo[id])-x);
  return sign ? -z : z;
}

long double atan2l(long double y, long double x) {
  if (isnan(x) || isnan(y)) return x+y;
  if (x == 1) return reduced_atan(y);
  union atan_ldshape ux = {.f = x}, uy = {.f = y};
  int ex = ux.i.se & 0x7fff, ey = uy.i.se & 0x7fff;
  int m = 2*(ux.i.se >> 15) | (uy.i.se >> 15);
  if (y == 0) {
    if (!(m & 2)) return y;
    return m & 1 ? -2*pio2_hi : 2*pio2_hi;
  }
  if (x == 0) return m & 1 ? -pio2_hi : pio2_hi;
  if (ex == 0x7fff) {
    if (ey == 0x7fff) {
      long double angle = m & 2 ? 1.5L*pio2_hi : pio2_hi/2;
      return m & 1 ? -angle : angle;
    }
    if (m & 2) return m & 1 ? -2*pio2_hi : 2*pio2_hi;
    return m & 1 ? -0.0L : 0.0L;
  }
  if (ex+120 < ey || ey == 0x7fff) return m & 1 ? -pio2_hi : pio2_hi;
  long double z = (m & 2) && ey+120 < ex ? 0 : reduced_atan(fabsl(y/x));
  switch (m) {
  case 0: return z;
  case 1: return -z;
  case 2: return 2*pio2_hi-(z-2*pio2_lo);
  default: return (z-2*pio2_lo)-2*pio2_hi;
  }
}
