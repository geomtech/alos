#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(test) do { \
  if (!(test)) { \
    printf("libc-common-test: FAIL line %d errno=%d\n", __LINE__, errno); \
    ++failures; \
  } \
} while (0)

static uint32_t bits(float x) {
  return ((union { float f; uint32_t u; }){ .f = x }).u;
}

static float from_bits(uint32_t u) {
  return ((union { uint32_t u; float f; }){ .u = u }).f;
}

static void check_expf(void) {
  /* References Decimal exp (100 chiffres), arrondies binary32 nearest-even. */
  static const struct { uint32_t input, expected; } cases[] = {
    {0x00000000u, 0x3f800000u}, {0x80000000u, 0x3f800000u},
    {0x3f800000u, 0x402df854u}, {0xbf800000u, 0x3ebc5ab2u},
    {0x3f000000u, 0x3fd3094cu}, {0xbf000000u, 0x3f1b4598u},
    {0x42b00000u, 0x7ef882b7u}, {0x42b17217u, 0x7f7fff84u},
    {0x42b17218u, 0x7f800000u}, {0x42b17219u, 0x7f800000u},
    {0xc2aeac50u, 0x007fffe6u}, {0xc2aeac4fu, 0x00800026u},
    {0xc2aeac51u, 0x007fffa6u}, {0xc2cff1b3u, 0x00000001u},
    {0xc2cff1b4u, 0x00000001u}, {0xc2cff1b5u, 0x00000000u},
    {0xc2c80000u, 0x0000001bu}, {0x35800000u, 0x3f800008u},
    {0xb5800000u, 0x3f7ffff0u}, {0x3c317217u, 0x3f8164d2u},
    {0xbc317217u, 0x3f7d3e0cu},
  };
  float (*volatile call)(float) = expf;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    errno = EDOM;
    uint32_t actual = bits(call(from_bits(cases[i].input)));
    if (actual != cases[i].expected) {
      printf("libc-common-test: expf input=%08x expected=%08x actual=%08x\n",
             cases[i].input, cases[i].expected, actual);
      ++failures;
    }
    int range = cases[i].expected < 0x00800000u ||
                cases[i].expected == 0x7f800000u;
    CHECK(errno == (range ? ERANGE : EDOM));
  }
  errno = EDOM;
  CHECK(bits(call(INFINITY)) == 0x7f800000u && errno == EDOM);
  CHECK(bits(call(-INFINITY)) == 0 && errno == EDOM);
  CHECK(isnan(call(NAN)) && errno == EDOM);
  CHECK(bits(call(from_bits(1))) == 0x3f800000u);
  CHECK(bits(call(from_bits(0x80000001u))) == 0x3f800000u);
  float previous = 0;
  for (int i = -1600; i <= 1408; ++i) {
    float value = call((float)i / 16);
    CHECK(isfinite(value) && value > 0 && value >= previous);
    previous = value;
  }
}

static void check_atan2(void) {
  const float pi = 0x1.921fb6p1f;
  const long double pil = 3.141592653589793238462643383279502884L;
  float (*volatile callf)(float, float) = atan2f;
  long double (*volatile calll)(long double, long double) = atan2l;
  for (int sx = 0; sx < 2; ++sx) {
    for (int sy = 0; sy < 2; ++sy) {
      float x = sx ? -1 : 1, y = sy ? -0.0f : 0.0f;
      float result = callf(y, x);
      long double resultl = calll((long double)y, (long double)x);
      CHECK(bits(result) == bits(sx ? (sy ? -pi : pi) : y));
      CHECK(resultl == (sx ? (sy ? -pil : pil) : (long double)y));
      CHECK(!!signbit(resultl) == sy);
      CHECK(bits(callf(y, sx ? -0.0f : 0.0f)) == bits(result));
      CHECK(calll((long double)y, sx ? -0.0L : 0.0L) == resultl);
      float infx = sx ? -INFINITY : INFINITY;
      float infy = sy ? -INFINITY : INFINITY;
      float angle = sx ? 3*pi/4 : pi/4;
      long double anglel = sx ? 3*pil/4 : pil/4;
      CHECK(bits(callf(infy, infx)) == bits(sy ? -angle : angle));
      CHECK(calll((long double)infy, (long double)infx) == (sy ? -anglel : anglel));
      CHECK(bits(callf(sy ? -1 : 1, infx)) == bits(result));
      CHECK(calll(sy ? -1 : 1, (long double)infx) == resultl);
      uint32_t inf_actual = bits(callf(infy, x));
      uint32_t inf_expected = bits(sy ? -pi/2 : pi/2);
      if (inf_actual != inf_expected) {
        printf("libc-common-test: atan2f infinity sx=%d sy=%d actual=%08x reference=%08x errno=%d\n",
               sx, sy, inf_actual, inf_expected, errno);
        ++failures;
      }
      CHECK(calll((long double)infy, x) == (sy ? -pil/2 : pil/2));
      CHECK(bits(callf(sy ? -1 : 1, sx ? -0.0f : 0.0f)) ==
            bits(sy ? -pi/2 : pi/2));
    }
  }
  CHECK(isnan(callf(NAN, 1)) && isnan(callf(1, NAN)));
  CHECK(isnan(calll(NAN, 1)) && isnan(calll(1, NAN)));
  static const struct { float y, x; uint32_t expected; } cases[] = {
    {1, 2, 0x3eed6338}, {2, 1, 0x3f8db70d},
    {1, -2, 0x402b6374}, {-2, -1, 0xc0023454},
    {0.4375f, 1, 0x3ed32776}, {0.6875f, 1, 0x3f1a2f81},
    {1.1875f, 1, 0x3f5ef387}, {2.4375f, 1, 0x3f973ab9},
    {1e-20f, 1, 0x1e3ce508}, {1, 1e20f, 0x1e3ce508}
  };
  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
    uint32_t actual = bits(callf(cases[i].y, cases[i].x));
    uint32_t distance = actual > cases[i].expected
        ? actual-cases[i].expected : cases[i].expected-actual;
    CHECK(distance <= 1);
  }
  static const struct { long double y, x, expected; } casesl[] = {
    {1, 2, 0.463647609000806116214256231461214402L},
    {2, 1, 1.107148717794090503017065460178537040L},
    {1, -2, 2.677945044588987122248387151818288483L},
    {-2, -1, -2.034443935795702735445577923100965845L},
    {0.4375L, 1, 0.412410441597387306899791289667126937L}
  };
  for (size_t i = 0; i < sizeof(casesl)/sizeof(casesl[0]); ++i)
    CHECK(fabsl(calll(casesl[i].y, casesl[i].x)-casesl[i].expected) <= 0x1p-61L);
  /* Ces resultats sont impossibles avec un intermediaire binary64. */
  CHECK(calll(0x1p-1000L, 0x1p1000L) == 0x1p-2000L);
  CHECK(calll(0x1p-16445L, 1) == 0x1p-16445L);
  CHECK(calll(1, 1) != (long double)(double)(pil/4));
  CHECK(calll(0x1p16000L, 0x1p-16000L) == pil/2);
  CHECK(calll(0x1p-16000L, -0x1p16000L) == pil);
  errno = EDOM;
  CHECK(callf(0, 0) == 0 && calll(0, 0) == 0 && errno == EDOM);
}

static void check_atol(void) {
  CHECK(atol(" \t\n\r\v\f-123tail") == -123);
  CHECK(atol("+456!") == 456);
  CHECK(atol("0012") == 12);
  CHECK(atol("0x20") == 0);
  CHECK(atol("") == 0 && atol("word") == 0 && atol("-") == 0);
  CHECK(atol("9223372036854775807") == LONG_MAX);
  CHECK(atol("-9223372036854775808") == LONG_MIN);
  errno = EDOM;
  CHECK(atol("42") == 42 && errno == EDOM);
  errno = 0;
  CHECK(atol("9223372036854775808") == LONG_MAX && errno == ERANGE);
}

static void check_output(void) {
  errno = EDOM;
  CHECK(fputs("", stdout) >= 0 && errno == EDOM);
  CHECK(fputs("libc-common-test: stdout[", stdout) >= 0);
  CHECK(fputs("no-newline", stdout) >= 0);
  CHECK(fputs("]stdout-end\n", stdout) >= 0 && !ferror(stdout));
  CHECK(fputs("libc-common-test: stderr[bytes]stderr-end\n", stderr) >= 0);
  errno = EINVAL;
  perror("libc-common-test: perror-prefix");
  CHECK(errno == EINVAL && !ferror(stderr));
  errno = ERANGE;
  perror("");
  CHECK(errno == ERANGE && !ferror(stderr));
  errno = EIO;
  perror(NULL);
  CHECK(errno == EIO && !ferror(stderr));

  FILE closed = {.fd = -1};
  errno = 0;
  CHECK(fputs("x", &closed) == EOF && errno == EBADF && ferror(&closed));
  FILE *saved_stderr = stderr;
  stderr = &closed;
  clearerr(&closed);
  errno = EINVAL;
  perror("not-writable");
  stderr = saved_stderr;
  CHECK(errno == EBADF && ferror(&closed));

  int fd = open("/wide-io-output", O_RDWR);
  CHECK(fd >= 0);
  if (fd >= 0) {
    FILE file = {.fd = fd};
    errno = 0;
    CHECK(fputs("changed", &file) >= 0 && !ferror(&file));
    stderr = &file;
    clearerr(&file);
    errno = EINVAL;
    perror("file-write");
    stderr = saved_stderr;
    CHECK(errno == EINVAL && !ferror(&file));
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    const char *expected = "changedfile-write: Invalid argument\n";
    char data[128] = {0};
    CHECK(read(fd, data, sizeof(data)) == (ssize_t)strlen(expected) &&
          !memcmp(data, expected, strlen(expected)));
    CHECK(close(fd) == 0);
    fd = open("/wide-io-output", O_RDONLY);
    CHECK(fd >= 0);
    if (fd >= 0) {
      char persisted[128] = {0};
      CHECK(read(fd, persisted, sizeof(persisted)) == (ssize_t)strlen(expected) &&
            !memcmp(persisted, expected, strlen(expected)));
      FILE readonly = {.fd = fd};
      errno = 0;
      CHECK(fputs("blocked", &readonly) == EOF &&
            errno == EBADF && ferror(&readonly));
      CHECK(close(fd) == 0);
    }
  }
}

int main(void) {
  check_expf();
  check_atan2();
  check_atol();
  check_output();
  if (failures) {
    printf("libc-common-test: FAIL %d\n", failures);
    return 1;
  }
  puts("libc-common-test: PASS");
  return 0;
}
