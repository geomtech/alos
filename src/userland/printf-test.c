#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures;

static void check(const char *expected, const char *format, ...) {
  char buffer[256];
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  if (result != (int)strlen(expected) || strcmp(buffer, expected)) {
    fprintf(stderr, "printf-test: mismatch expected='%s' actual='%s' length=%d\n",
            expected, buffer, result);
    ++failures;
  }
}

int main(void) {
  check("-9223372036854775808", "%lld", LLONG_MIN);
  check("-9223372036854775808 18446744073709551615 ffffffffffffffff",
        "%" PRId64 " %" PRIuMAX " %" PRIxPTR,
        INT64_MIN, UINTMAX_MAX, UINTPTR_MAX);
  check("-7 42", "%" PRIdFAST16 " %" PRIuLEAST8,
        (int_fast16_t)-7, (uint_least8_t)42);
  check("0xff 011 00042", "%#x %#o %05d", 255, 9, 42);
  check("     007|abc  |", "%8.3d|%-5.3s|", 7, "abcdef");
  check("22 11", "%2$d %1$d", 11, 22);
  check("1.250000 -0.00 1.25e+01 123", "%f %.2f %.2e %.3g",
        1.25, -0.0, 12.5, 123.0);
  check("0x1.8p+0", "%a", 1.5);
  check("1.125", "%.3Lf", 1.125L);
  check("inf -inf nan", "%g %g %g", INFINITY, -INFINITY, NAN);
  check("wide Z", "%ls %lc", L"wide", (wint_t)L'Z');
  check("\xc3\xa9", "%ls", L"\u00e9");
  check("abc", "%.3s", "abcdef");

  char buffer[8];
  memset(buffer, '!', sizeof(buffer));
  if (snprintf(buffer, 4, "%s", "abcdef") != 6 ||
      memcmp(buffer, "abc\0!!!!", sizeof(buffer))) ++failures;
  buffer[0] = '!';
  if (snprintf(buffer, 1, "abc") != 3 || buffer[0] != '\0') ++failures;
  buffer[0] = '!';
  if (snprintf(buffer, 0, "abc") != 3 || buffer[0] != '!') ++failures;
  if (snprintf(NULL, 0, "%s:%d", "abcdef", 42) != 9) ++failures;
  int count = -1;
  if (snprintf(buffer, sizeof(buffer), "abc%n", &count) != 3 ||
      count != 3 || strcmp(buffer, "abc")) ++failures;
  char invalid[] = "%";
  errno = 0;
  if (snprintf(buffer, sizeof(buffer), invalid) != -1 || errno != EINVAL)
    ++failures;

  char long_text[1501];
  memset(long_text, 'x', sizeof(long_text) - 1);
  long_text[sizeof(long_text) - 1] = '\0';
  if (printf("%s\n", long_text) != 1501) ++failures;
  FILE broken = {-1, 0, 0};
  if (fprintf(&broken, "write failure") != EOF || !ferror(&broken)) ++failures;
  if (failures) { puts("printf-test: FAIL"); return 1; }
  puts("printf-test: PASS");
  return 0;
}
