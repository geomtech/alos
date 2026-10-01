#include <stdint.h>
#include <stdio.h>

typedef unsigned __int128 wide_t;
typedef __int128 signed_wide_t;
extern wide_t __udivti3(wide_t, wide_t);
extern wide_t __umodti3(wide_t, wide_t);
extern wide_t __udivmodti4(wide_t, wide_t, wide_t *);
extern signed_wide_t __divti3(signed_wide_t, signed_wide_t);
extern signed_wide_t __modti3(signed_wide_t, signed_wide_t);

static int check(wide_t n, wide_t d, wide_t q, wide_t r) {
  wide_t remainder;
  return __udivti3(n, d) == q && __umodti3(n, d) == r &&
         __udivmodti4(n, d, &remainder) == q && remainder == r;
}

int main(void) {
  wide_t maximum = ~(wide_t)0, high = (wide_t)1 << 127;
  if (!check(0, maximum, 0, 0) ||
      !check(maximum, 1, maximum, 0) ||
      !check(maximum, 2, maximum >> 1, 1) ||
      !check(maximum, maximum, 1, 0) ||
      !check(maximum, high, 1, high - 1) ||
      !check(maximum, ((wide_t)1 << 64) + 1, UINT64_MAX, 0) ||
      !check(high + (high >> 1) + 7, high + 9, 1, (high >> 1) - 2)) {
    puts("int128-runtime-test: FAIL limits");
    return 1;
  }
  if (__divti3(-1234567890123456789LL, 97) != -12727504021891307LL ||
      __modti3(-1234567890123456789LL, 97) != -10 ||
      __divti3(1234567890123456789LL, -97) != -12727504021891307LL ||
      __modti3(1234567890123456789LL, -97) != 10) {
    puts("int128-runtime-test: FAIL signed");
    return 1;
  }
  for (unsigned i = 0; i < 128; i += 7) {
    for (unsigned j = 0; j < 128; j += 7) {
      wide_t n = (wide_t)1 << i, d = (wide_t)1 << j;
      wide_t q = i >= j ? (wide_t)1 << (i - j) : 0;
      wide_t r = i < j ? n : 0;
      if (!check(n, d, q, r)) {
        printf("int128-runtime-test: FAIL shift=%u/%u\n", i, j);
        return 1;
      }
    }
  }
  puts("int128-runtime-test: PASS");
  return 0;
}
