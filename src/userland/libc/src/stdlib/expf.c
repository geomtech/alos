/*
 * Single-precision e^x, adapted from musl 1.2.6 expf.c/exp2f_data.c.
 * Copyright (c) 2017-2018, Arm Limited.
 * SPDX-License-Identifier: MIT
 * See src/userland/libc/COPYRIGHT.musl.
 *
 * Table bits = 5, polynomial order = 3.
 * Upstream ULP error: 0.502 (nearest rounding).
 * Relative error before rounding: 1.69 * 2^-34.
 * Non-nearest rounded ULP error: 1.
 */
#include <math.h>
#include <stdint.h>
#include <errno.h>

static uint32_t float_bits(float x) {
  return ((union { float f; uint32_t i; }){ .f = x }).i;
}

static const uint64_t expf_table[32] = {
  0x3ff0000000000000, 0x3fefd9b0d3158574, 0x3fefb5586cf9890f, 0x3fef9301d0125b51,
  0x3fef72b83c7d517b, 0x3fef54873168b9aa, 0x3fef387a6e756238, 0x3fef1e9df51fdee1,
  0x3fef06fe0a31b715, 0x3feef1a7373aa9cb, 0x3feedea64c123422, 0x3feece086061892d,
  0x3feebfdad5362a27, 0x3feeb42b569d4f82, 0x3feeab07dd485429, 0x3feea47eb03a5585,
  0x3feea09e667f3bcd, 0x3fee9f75e8ec5f74, 0x3feea11473eb0187, 0x3feea589994cce13,
  0x3feeace5422aa0db, 0x3feeb737b0cdc5e5, 0x3feec49182a3f090, 0x3feed503b23e255d,
  0x3feee89f995ad3ad, 0x3feeff76f2fb5e47, 0x3fef199bdd85529c, 0x3fef3720dcef9069,
  0x3fef5818dcfba487, 0x3fef7c97337b9b5f, 0x3fefa4afa2a490da, 0x3fefd0765b6e4540,
};

float expf(float x) {
  uint32_t bits = float_bits(x);
  uint32_t abstop = (bits >> 20) & 0x7ff;
  if (abstop >= (float_bits(88.0f) >> 20)) {
    if (bits == float_bits(-INFINITY)) return 0.0f;
    if (abstop >= (float_bits(INFINITY) >> 20)) return x + x;
    if (x > 0x1.62e42ep6f) {
      volatile float huge = 0x1p97f;
      errno = ERANGE;
      return huge * huge;
    }
    if (x < -0x1.9fe368p6f) {
      volatile float tiny = 0x1p-95f;
      errno = ERANGE;
      return tiny * tiny;
    }
  }

  double z = (0x1.71547652b82fep+0 * 32) * (double)x;
  double kd = z + 0x1.8p52;
  uint64_t ki = ((union { double f; uint64_t i; }){ .f = kd }).i;
  kd -= 0x1.8p52;
  double r = z - kd;
  uint64_t t = expf_table[ki % 32] + (ki << (52 - 5));
  double s = ((union { uint64_t i; double f; }){ .i = t }).f;
  z = (0x1.c6af84b912394p-5 / 32 / 32 / 32) * r
      + (0x1.ebfce50fac4f3p-3 / 32 / 32);
  double r2 = r * r;
  double y = (0x1.62e42ff0c52d6p-1 / 32) * r + 1;
  y = z * r2 + y;
  float result = (float)(y * s);
  if (float_bits(result) < 0x00800000 || isinf(result)) errno = ERANGE;
  return result;
}
