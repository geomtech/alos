#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void check(const char *input, long double expected, const char *rest) {
  char *end;
  errno = 0;
  long double value = strtold(input, &end);
  long double delta = value - expected;
  if (delta < 0) delta = -delta;
  long double tolerance = expected < 0 ? -expected : expected;
  tolerance = tolerance * 1e-15L + 1e-30L;
  if (delta > tolerance || strcmp(end, rest) || errno) {
    printf("strtod-test: FAIL '%s' end='%s' errno=%d\n", input, end, errno);
    ++failures;
  }
}
int main(void) {
  check("0", 0, ""); check("1", 1, ""); check("-1", -1, "");
  check("1.5", 1.5L, ""); check("-0.125", -0.125L, "");
  check("123456.789", 123456.789L, "");
  check("1e10", 1e10L, ""); check("1e-10", 1e-10L, "");
  check("-2.5E+20", -2.5e20L, "");
  check("12abc", 12, "abc"); check("abc", 0, "abc"); check("", 0, "");
  check("  +0x1.8p+2!", 6, "!"); check("1e+", 1, "e+");
  char *end;
  if (!isinf(strtod("inf", &end)) || *end ||
      !isinf(strtof("-INFINITY", &end)) || *end ||
      !isnan(strtold("nan(payload)", &end)) || *end) ++failures;
  errno = 0;
  if (!isinf(strtof("1e100", &end)) || errno != ERANGE || *end) ++failures;
  errno = 0;
  if (strtod("1e-999", &end) != 0 || errno != ERANGE || *end) ++failures;
  errno = 0;
  if (strtof("3.402823466385288598e38", &end) != 0x1.fffffep127f || errno) ++failures;
  errno = 0;
  if (strtod("2.2250738585072014e-308", &end) != 0x1p-1022 || errno) ++failures;
  errno = 0;
  if (strtof("1.1754943508222875e-38", &end) != 0x1p-126f || errno) ++failures;
  errno = 0;
  if (strtod("1.7976931348623157e308", &end) != 0x1.fffffffffffffp1023 || errno) ++failures;
  errno = 0;
  if (strtof("1e-44", &end) == 0 || errno != ERANGE || *end) ++failures;
  if (failures) { puts("strtod-test: FAIL"); return 1; }
  puts("strtod-test: PASS");
  return 0;
}
