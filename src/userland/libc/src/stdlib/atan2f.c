/* Adapted from musl 1.2.6 atan2f.c and atanf.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support.
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 * See src/userland/libc/COPYRIGHT.musl.
 */
#include <math.h>
#include <stdint.h>

static uint32_t word(float x) {
  return ((union { float f; uint32_t i; }){ .f = x }).i;
}
static float absolute(float x) { return __builtin_fabsf(x); }

static float reduced_atan(float x) {
  static const float hi[] = {
    4.6364760399e-01f, 7.8539812565e-01f,
    9.8279368877e-01f, 1.5707962513e+00f
  };
  static const float lo[] = {
    5.0121582440e-09f, 3.7748947079e-08f,
    3.4473217170e-08f, 7.5497894159e-08f
  };
  static const float a[] = {
    3.3333328366e-01f, -1.9999158382e-01f, 1.4253635705e-01f,
    -1.0648017377e-01f, 6.1687607318e-02f
  };
  uint32_t ix = word(x), sign = ix >> 31;
  ix &= 0x7fffffff;
  int id;
  float z, w, s1, s2;
  if (ix >= 0x4c800000) {
    if (isnan(x)) return x;
    /* hi seul est le voisin inferieur de pi/2 ; inclure sa correction. */
    z = hi[3] + lo[3];
    return sign ? -z : z;
  }
  if (ix < 0x3ee00000) {
    if (ix < 0x39800000) {
      if (ix < 0x00800000) { volatile float tiny = x*x; (void)tiny; }
      return x;
    }
    id = -1;
  } else {
    x = absolute(x);
    if (ix < 0x3f980000) {
      if (ix < 0x3f300000) { id = 0; x = (2*x-1)/(2+x); }
      else { id = 1; x = (x-1)/(x+1); }
    } else {
      if (ix < 0x401c0000) { id = 2; x = (x-1.5f)/(1+1.5f*x); }
      else { id = 3; x = -1/x; }
    }
  }
  z = x*x;
  w = z*z;
  s1 = z*(a[0]+w*(a[2]+w*a[4]));
  s2 = w*(a[1]+w*a[3]);
  if (id < 0) return x-x*(s1+s2);
  z = hi[id]-((x*(s1+s2)-lo[id])-x);
  return sign ? -z : z;
}

float atan2f(float y, float x) {
  static const float pi = 3.1415927410e+00f, pi_lo = -8.7422776573e-08f;
  if (isnan(x) || isnan(y)) return x+y;
  uint32_t ix = word(x), iy = word(y);
  if (ix == 0x3f800000) return reduced_atan(y);
  unsigned m = ((iy >> 31) & 1) | ((ix >> 30) & 2);
  ix &= 0x7fffffff;
  iy &= 0x7fffffff;
  if (iy == 0) {
    if (!(m & 2)) return y;
    return m & 1 ? -pi : pi;
  }
  if (ix == 0) return m & 1 ? -pi/2 : pi/2;
  if (ix == 0x7f800000) {
    if (iy == 0x7f800000) {
      float angle = m & 2 ? 3*pi/4 : pi/4;
      return m & 1 ? -angle : angle;
    }
    if (m & 2) return m & 1 ? -pi : pi;
    return m & 1 ? -0.0f : 0.0f;
  }
  if (ix+(26u << 23) < iy || iy == 0x7f800000)
    return m & 1 ? -pi/2 : pi/2;
  float z = (m & 2) && iy+(26u << 23) < ix
      ? 0 : reduced_atan(absolute(y/x));
  switch (m) {
  case 0: return z;
  case 1: return -z;
  case 2: return pi-(z-pi_lo);
  default: return (z-pi_lo)-pi;
  }
}
