#include <math.h>
#include <stdint.h>
#include <errno.h>

float scalbnf(float x, int n) {
  /* Double garde les produits exacts jusqu'a l'arrondi binaire32 final. */
  double y = x;
  if (n > 127) {
    y *= 0x1p127;
    n -= 127;
    if (n > 127) {
      y *= 0x1p127;
      n -= 127;
      if (n > 127) n = 127;
    }
  } else if (n < -126) {
    y *= 0x1p-126;
    n += 126;
    if (n < -126) {
      y *= 0x1p-126;
      n += 126;
      if (n < -126) n = -126;
    }
  }
  union { uint32_t bits; float value; } scale = {
    .bits = (uint32_t)(127 + n) << 23
  };
  return (float)(y * scale.value);
}

float ldexpf(float x, int n) {
  float result = scalbnf(x, n);
  if (isfinite(x) && x != 0.0f &&
      (isinf(result) || result == 0.0f)) errno = ERANGE;
  return result;
}
