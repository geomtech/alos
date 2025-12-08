/* src/userland/libc/src/stdlib/atoi.c */
#include <ctype.h>
#include <stdlib.h>

int atoi(const char *str) {
  int result = 0;
  int sign = 1;
  while (isspace(*str))
    str++;
  if (*str == '-') {
    sign = -1;
    str++;
  } else if (*str == '+') {
    str++;
  }
  while (isdigit(*str)) {
    result = result * 10 + (*str - '0');
    str++;
  }
  return sign * result;
}

char *itoa(int value, char *str, int base) {
  static const char digits[] = "0123456789ABCDEF";
  char *ptr = str;
  char *ptr1 = str;
  char tmp_char;
  int tmp_value;
  int negative = 0;

  if (base < 2 || base > 16) {
    *str = '\0';
    return str;
  }

  if (value < 0 && base == 10) {
    negative = 1;
    value = -value;
  }

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = digits[tmp_value - value * base];
  } while (value);

  if (negative)
    *ptr++ = '-';
  *ptr-- = '\0';

  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return str;
}
