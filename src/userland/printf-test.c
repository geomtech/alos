#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <locale.h>

static int failures;
static int compare_ints(const void *left, const void *right) {
  int a = *(const int *)left, b = *(const int *)right;
  return (a > b) - (a < b);
}

static int scan(const char *text, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsscanf(text, format, args);
  va_end(args);
  return result;
}

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
  const int sorted[] = {-10, -3, 0, 1, 7, 20};
  for (size_t i = 0; i < sizeof(sorted) / sizeof(sorted[0]); ++i)
    if (bsearch(&sorted[i], sorted, 6, sizeof(int), compare_ints) != &sorted[i])
      ++failures;
  int missing = 5;
  if (bsearch(&missing, sorted, 6, sizeof(int), compare_ints) ||
      bsearch(&missing, sorted, 0, sizeof(int), compare_ints)) ++failures;
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
  char *allocated = NULL;
  if (asprintf(&allocated, "%s %.2f", "allocated", 1.25) != 14 ||
      !allocated || strcmp(allocated, "allocated 1.25")) ++failures;
  free(allocated);
  locale_t c_locale = newlocale(LC_ALL_MASK, "C", NULL);
  char *end;
  if (!c_locale || strtod_l("1.25rest", &end, c_locale) != 1.25 ||
      strcmp(end, "rest") || strtof_l("2.5", &end, c_locale) != 2.5f ||
      *end || strtold_l("3.75", &end, c_locale) != 3.75L || *end)
    ++failures;
  if (c_locale) freelocale(c_locale);
  int number = 0, consumed = -1;
  unsigned hexadecimal = 0;
  char word[8], tail = 0;
  long double extended = 0;
  double real = 0;
  float single = 0;
  if (scan(" -42 0xff 1.125 abc!", "%d %x %Lf %3s%n%c",
           &number, &hexadecimal, &extended, word, &consumed, &tail) != 5 ||
      number != -42 || hexadecimal != 255 || extended != 1.125L ||
      strcmp(word, "abc") || consumed != 19 || tail != '!') ++failures;
  if (sscanf("1.25 0x1.8p+0", "%lf %f", &real, &single) != 2 ||
      real != 1.25 || single != 1.5f) ++failures;
  if (sscanf("12345", "%3d%d", &number, &consumed) != 2 ||
      number != 123 || consumed != 45) ++failures;
  if (sscanf("abc123", "%3[a-z]%d", word, &number) != 2 ||
      strcmp(word, "abc") || number != 123) ++failures;
  if (sscanf("99 7", "%*d%d", &number) != 1 || number != 7 ||
      sscanf("", "%d", &number) != EOF ||
      sscanf("word", "%d", &number) != 0 ||
      sscanf("4", "%d%d", &number, &consumed) != 1 ||
      sscanf("100er", "%f", &single) != 0) ++failures;
  wchar_t wide[8];
  if (sscanf("\xc3\xa9lan", "%ls", wide) != 1 ||
      wcscmp(wide, L"\u00e9lan")) ++failures;
  stdin->eof = 1;
  if (ungetc('A', stdin) != 'A' || feof(stdin) ||
      ungetc('B', stdin) != EOF || getc(stdin) != 'A' ||
      ungetc(EOF, stdin) != EOF) ++failures;

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
