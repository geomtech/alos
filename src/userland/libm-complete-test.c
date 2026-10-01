#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(expr) do { \
  if (!(expr)) { \
    printf("libm-complete-test: FAIL line %d errno=%d\n", __LINE__, errno); \
    ++failures; \
  } \
} while (0)

static uint32_t fbits(float x) { return ((union { float f; uint32_t u; }){x}).u; }
static uint64_t dbits(double x) { return ((union { double f; uint64_t u; }){x}).u; }

static int close_double(double a, double b, double tol) {
  return fabs(a - b) <= tol;
}

static int close_float(float a, float b, float tol) {
  return fabsf(a - b) <= tol;
}

static int close_ld(long double a, long double b, long double tol) {
  long double d = a > b ? a - b : b - a;
  return d <= tol;
}

static void check_rounding(void) {
  errno = EDOM;
  CHECK(fbits(floorf(-1.25f)) == fbits(-2.0f) && errno == EDOM);
  CHECK(fbits(floorf(0.0f)) == 0 && fbits(floorf(-0.0f)) == 0x80000000u);
  CHECK(fbits(truncf(-1.75f)) == fbits(-1.0f));
  CHECK(fbits(roundf(2.5f)) == fbits(3.0f) && fbits(roundf(-2.5f)) == fbits(-3.0f));
  CHECK(fbits(rintf(2.5f)) == fbits(2.0f));
  CHECK(fbits(nearbyintf(-2.5f)) == fbits(-2.0f));
  CHECK(lrintf(2.5f) == 2 && lrintf(-2.5f) == -2);
  CHECK(lroundf(2.5f) == 3 && lroundf(-2.5f) == -3);
  CHECK(llrintf(3.5f) == 4 && llroundf(-3.5f) == -4);
  CHECK(fbits(fmaf(2.0f, 3.0f, 4.0f)) == fbits(10.0f));
  CHECK(fbits(fmaf(0x1p-70f, 0x1p-70f, 0x1p-149f)) == 0x00000201u);
}

static void check_elementary(void) {
  const double pi = M_PI;
  CHECK(close_double(sin(pi / 6.0), 0.5, 0x1p-52));
  CHECK(close_double(cos(pi / 3.0), 0.5, 0x1p-52));
  CHECK(close_double(tan(pi / 4.0), 1.0, 0x1p-50));
  CHECK(close_float(sinf((float)pi / 6.0f), 0.5f, 0x1p-23f));
  CHECK(close_float(cosf((float)pi / 3.0f), 0.5f, 0x1p-23f));
  CHECK(close_float(tanf((float)pi / 4.0f), 1.0f, 0x1p-22f));
  CHECK(pow(2.0, 10.0) == 1024.0 && powf(2.0f, 10.0f) == 1024.0f);
  CHECK(log(1.0) == 0.0 && logf(1.0f) == 0.0f);
  CHECK(exp2(10.0) == 1024.0 && exp2f(10.0f) == 1024.0f);
  CHECK(cbrt(27.0) == 3.0 && cbrtf(27.0f) == 3.0f);
  CHECK(hypot(3.0, 4.0) == 5.0 && hypotf(3.0f, 4.0f) == 5.0f);
  CHECK(fmod(5.5, 2.0) == 1.5 && fmodf(5.5f, 2.0f) == 1.5f);
  int quo = 0;
  CHECK(remainder(5.0, 2.0) == 1.0);
  CHECK(remquo(7.0, 2.0, &quo) == -1.0 && (quo & 7) == 4);
  quo = 0;
  CHECK(remquof(7.0f, 2.0f, &quo) == -1.0f && (quo & 7) == 4);
  CHECK(dbits(nextafter(1.0, 2.0)) == 0x3ff0000000000001ull);
  CHECK(fbits(nextafterf(1.0f, 2.0f)) == 0x3f800001u);
  CHECK(fdim(5.0, 3.0) == 2.0 && fdimf(3.0f, 5.0f) == 0.0f);
  CHECK(fmax(NAN, 4.0) == 4.0 && fminf(NAN, -2.0f) == -2.0f);
  CHECK(copysign(1.0, -0.0) == -1.0 && fbits(copysignf(1.0f, -0.0f)) == fbits(-1.0f));
  CHECK(fabs(-3.0) == 3.0 && fabsf(-3.0f) == 3.0f);
  CHECK(ilogb(8.0) == 3 && ilogbf(8.0f) == 3);
  CHECK(logb(8.0) == 3.0 && logbf(8.0f) == 3.0f);
  int e = 0;
  CHECK(frexp(8.0, &e) == 0.5 && e == 4);
  CHECK(ldexp(0.5, 4) == 8.0 && scalbn(0.5, 4) == 8.0 && scalbln(0.5, 4) == 8.0);
  float ipf;
  CHECK(modff(-2.75f, &ipf) == -0.75f && ipf == -2.0f);
  CHECK(fbits(nanf("")) == 0x7fc00000u && isnan(nan("")));
}

static void check_long_double(void) {
  CHECK(sqrtl(4.0L) == 2.0L);
  CHECK(floorl(-1.25L) == -2.0L && ceill(-1.25L) == -1.0L && truncl(-1.25L) == -1.0L);
  CHECK(roundl(2.5L) == 3.0L && rintl(2.5L) == 2.0L);
  CHECK(lrintl(2.5L) == 2 && llrintl(3.5L) == 4);
  CHECK(lroundl(-2.5L) == -3 && llroundl(-3.5L) == -4);
  CHECK(fmodl(5.5L, 2.0L) == 1.5L && remainderl(5.0L, 2.0L) == 1.0L);
  int quo = 0;
  CHECK(remquol(5.0L, 2.0L, &quo) == 1.0L);
  CHECK(expl(0.0L) == 1.0L && exp2l(10.0L) == 1024.0L);
  CHECK(logl(1.0L) == 0.0L && close_ld(log2l(1024.0L), 10.0L, 0x1p-60L) && close_ld(log10l(1000.0L), 3.0L, 0x1p-60L));
  CHECK(powl(2.0L, 10.0L) == 1024.0L && cbrtl(27.0L) == 3.0L);
  CHECK(close_ld(sinl(3.141592653589793238462643383279502884L / 6.0L), 0.5L, 0x1p-60L));
  CHECK(close_ld(cosl(3.141592653589793238462643383279502884L / 3.0L), 0.5L, 0x1p-60L));
  CHECK(close_ld(tanl(3.141592653589793238462643383279502884L / 4.0L), 1.0L, 0x1p-60L));
  CHECK(hypotl(3.0L, 4.0L) == 5.0L);
  long double ip;
  CHECK(modfl(-2.75L, &ip) == -0.75L && ip == -2.0L);
  CHECK(nextafterl(1.0L, 2.0L) > 1.0L && nexttowardl(1.0L, 2.0L) > 1.0L);
  CHECK(fmal(2.0L, 3.0L, 4.0L) == 10.0L);
  CHECK(fdiml(5.0L, 3.0L) == 2.0L && fmaxl(NAN, 4.0L) == 4.0L && fminl(NAN, -2.0L) == -2.0L);
  CHECK(fabsl(-3.0L) == 3.0L && copysignl(1.0L, -0.0L) == -1.0L);
}

static void check_errno_policy(void) {
  errno = EDOM;
  CHECK(expf(1.0f) > 0.0f && errno == EDOM);
  errno = 0;
  CHECK(isinf(expf(1000.0f)) && errno == ERANGE);
  errno = 0;
  CHECK(expf(-1000.0f) == 0.0f && errno == ERANGE);
  errno = 0;
  CHECK(isnan(log(-1.0)) && errno == EDOM);
  errno = EDOM;
  CHECK(sqrt(-1.0) != sqrt(-1.0) && errno == EDOM);
}

int main(void) {
  check_rounding();
  check_elementary();
  check_long_double();
  check_errno_policy();
  if (failures) {
    printf("libm-complete-test: FAIL %d\n", failures);
    return 1;
  }
  puts("libm-complete-test: PASS");
  return 0;
}
