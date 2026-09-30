#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

static int digit(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return -1;
}

static uint64_t parse_integer(const char *text, char **end, int base,
                              int signed_result, int *negative, int *overflow) {
  const char *cursor = text;
  *negative = *overflow = 0;
  if (end) *end = (char *)text;
  if (base && (base < 2 || base > 36)) { errno = EINVAL; return 0; }
  while (isspace((unsigned char)*cursor)) cursor++;
  if (*cursor == '-' || *cursor == '+') *negative = *cursor++ == '-';
  if ((base == 0 || base == 16) && cursor[0] == '0' &&
      (cursor[1] == 'x' || cursor[1] == 'X') &&
      digit((unsigned char)cursor[2]) >= 0 && digit((unsigned char)cursor[2]) < 16) {
    base = 16;
    cursor += 2;
  }
  if (!base) base = *cursor == '0' ? 8 : 10;
  uint64_t limit = signed_result
      ? (uint64_t)INT64_MAX + (*negative ? 1 : 0) : UINT64_MAX;
  uint64_t quotient = limit / (unsigned)base;
  unsigned remainder = (unsigned)(limit % (unsigned)base);
  uint64_t value = 0;
  const char *start = cursor;
  for (;;) {
    int next = digit((unsigned char)*cursor);
    if (next < 0 || next >= base) break;
    if (value > quotient || (value == quotient && (unsigned)next > remainder))
      *overflow = 1;
    if (!*overflow) value = value * (unsigned)base + (unsigned)next;
    cursor++;
  }
  if (cursor != start && end) *end = (char *)cursor;
  if (*overflow) { errno = ERANGE; return limit; }
  return value;
}

long long strtoll(const char *text, char **end, int base) {
  int negative, overflow;
  uint64_t value = parse_integer(text, end, base, 1, &negative, &overflow);
  if (negative && value == (uint64_t)INT64_MAX + 1) return INT64_MIN;
  return negative ? -(long long)value : (long long)value;
}
long strtol(const char *text, char **end, int base) {
  return (long)strtoll(text, end, base);
}
unsigned long long strtoull(const char *text, char **end, int base) {
  int negative, overflow;
  uint64_t value = parse_integer(text, end, base, 0, &negative, &overflow);
  return negative && !overflow ? (uint64_t)0 - value : value;
}
unsigned long strtoul(const char *text, char **end, int base) {
  return (unsigned long)strtoull(text, end, base);
}
