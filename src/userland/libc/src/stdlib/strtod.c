/* String entry points for the musl-derived ALOS floating-point scanner. */
#include <stdlib.h>
#include <errno.h>
#include "float_scan.h"

static long double convert(const char *text, char **end, int precision) {
  int saved_errno = errno;
  FloatStream stream = {text, text, -1};
  long double value = __alos_floatscan(&stream, precision, 1);
  if (end) *end = (char *)(text + shcnt(&stream));
  if (!shcnt(&stream) && errno == EINVAL) errno = saved_errno;
  return value;
}

float strtof(const char *text, char **end) {
  return (float)convert(text, end, 0);
}
double strtod(const char *text, char **end) {
  return (double)convert(text, end, 1);
}
long double strtold(const char *text, char **end) {
  return convert(text, end, 2);
}
