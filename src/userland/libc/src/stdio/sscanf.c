#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../stdlib/float_scan.h"

enum length { DEFAULT, HH, H, L, LL, J, Z, T, BIGL };

static void store_integer(void *out, enum length length, uintmax_t value,
                          int is_signed) {
  if (is_signed) {
    switch (length) {
    case HH: *(signed char *)out = (signed char)value; break;
    case H: *(short *)out = (short)value; break;
    case L: *(long *)out = (long)value; break;
    case LL: *(long long *)out = (long long)value; break;
    case J: *(intmax_t *)out = (intmax_t)value; break;
    case Z: case T: *(ptrdiff_t *)out = (ptrdiff_t)value; break;
    default: *(int *)out = (int)value; break;
    }
  } else {
    switch (length) {
    case HH: *(unsigned char *)out = (unsigned char)value; break;
    case H: *(unsigned short *)out = (unsigned short)value; break;
    case L: *(unsigned long *)out = (unsigned long)value; break;
    case LL: *(unsigned long long *)out = (unsigned long long)value; break;
    case J: *(uintmax_t *)out = value; break;
    case Z: case T: *(size_t *)out = (size_t)value; break;
    default: *(unsigned *)out = (unsigned)value; break;
    }
  }
}

static int digit(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int vsscanf(const char *input, const char *format, va_list arguments) {
  if (!input || !format) { errno = EINVAL; return EOF; }
  const char *text = input;
  int assigned = 0, converted = 0;
  va_list args;
  va_copy(args, arguments);
  while (*format) {
    if (isspace((unsigned char)*format)) {
      do ++format; while (isspace((unsigned char)*format));
      while (isspace((unsigned char)*text)) ++text;
      continue;
    }
    if (*format != '%' || format[1] == '%') {
      if (*format == '%') ++format;
      if (!*text) goto input_failure;
      if (*text != *format) break;
      ++text;
      ++format;
      continue;
    }
    ++format;
    int suppressed = *format == '*';
    if (suppressed) ++format;
    size_t width = 0;
    while (isdigit((unsigned char)*format)) {
      unsigned d = *format++ - '0';
      if (width > (SIZE_MAX - d) / 10) goto invalid;
      width = width * 10 + d;
    }
    if (!width) width = SIZE_MAX;
    enum length length = DEFAULT;
    switch (*format) {
    case 'h': length = H; if (*++format == 'h') { length = HH; ++format; } break;
    case 'l': length = L; if (*++format == 'l') { length = LL; ++format; } break;
    case 'j': length = J; ++format; break;
    case 'z': length = Z; ++format; break;
    case 't': length = T; ++format; break;
    case 'L': length = BIGL; ++format; break;
    }
    char specifier = *format;
    if (!specifier) goto invalid;
    ++format;
    void *out = suppressed ? NULL : va_arg(args, void *);
    if (specifier == 'n') {
      if (length == BIGL) goto invalid;
      if (out) store_integer(out, length, (size_t)(text - input), 1);
      continue;
    }
    if (specifier != 'c' && specifier != '[')
      while (isspace((unsigned char)*text)) ++text;
    if (!*text) goto input_failure;
    if (specifier == 's' || specifier == 'c' || specifier == '[') {
      if (length != DEFAULT && length != L) goto invalid;
      unsigned char set[256] = {0};
      int invert = 0;
      if (specifier == '[') {
        if (*format == '^') { invert = 1; ++format; }
        if (*format == ']') { set[']'] = 1; ++format; }
        while (*format && *format != ']') {
          unsigned char first = (unsigned char)*format++;
          if (*format == '-' && format[1] && format[1] != ']' &&
              first <= (unsigned char)format[1]) {
            unsigned char last = (unsigned char)format[1];
            format += 2;
            for (unsigned c = first; c <= last; ++c) set[c] = 1;
          } else set[first] = 1;
        }
        if (*format != ']') goto invalid;
        ++format;
      }
      if (specifier == 'c' && width == SIZE_MAX) width = 1;
      size_t count = 0;
      mbstate_t state = {0};
      while (*text && count < width) {
        unsigned char c = (unsigned char)*text;
        if ((specifier == 's' && isspace(c)) ||
            (specifier == '[' && set[c] == invert)) break;
        if (length == L) {
          wchar_t wc;
          size_t bytes = mbrtowc(&wc, text, strlen(text), &state);
          if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            errno = EILSEQ;
            goto input_failure;
          }
          if (out) ((wchar_t *)out)[count] = wc;
          text += bytes;
        } else {
          if (out) ((char *)out)[count] = *text;
          ++text;
        }
        ++count;
      }
      if (!count) break;
      if (specifier == 'c' && count < width) goto input_failure;
      if (out && specifier != 'c') {
        if (length == L) ((wchar_t *)out)[count] = 0;
        else ((char *)out)[count] = 0;
      }
    } else if (strchr("aAeEfFgG", specifier)) {
      if (length != DEFAULT && length != L && length != BIGL) goto invalid;
      size_t available = strnlen(text, width);
      FloatStream stream = {text, text, -1, text + available};
      int precision = length == BIGL ? 2 : length == L ? 1 : 0;
      long double value = __alos_floatscan(&stream, precision, 0);
      size_t consumed = shcnt(&stream);
      if (!consumed) break;
      text += consumed;
      if (out) {
        if (length == BIGL) *(long double *)out = value;
        else if (length == L) *(double *)out = (double)value;
        else *(float *)out = (float)value;
      }
    } else if (strchr("diouxXp", specifier)) {
      if (length == BIGL || (specifier == 'p' && length != DEFAULT)) goto invalid;
      size_t available = strnlen(text, width);
      const char *end = text + available;
      int negative = 0, base = specifier == 'o' ? 8 :
          (specifier == 'x' || specifier == 'X' || specifier == 'p') ? 16 :
          specifier == 'i' ? 0 : 10;
      const char *cursor = text;
      if (*cursor == '-' || *cursor == '+') negative = *cursor++ == '-';
      if (cursor < end && *cursor == '0') {
        if ((!base || base == 16) && end - cursor >= 2 &&
            (cursor[1] == 'x' || cursor[1] == 'X')) {
          base = 16;
          cursor += 2;
        } else if (!base) base = 8;
      }
      if (!base) base = 10;
      const char *digits = cursor;
      uintmax_t value = 0;
      int overflow = 0;
      while (cursor < end) {
        int d = digit((unsigned char)*cursor);
        if (d < 0 || d >= base) break;
        if (value > (UINTMAX_MAX - (unsigned)d) / (unsigned)base) overflow = 1;
        else value = value * (unsigned)base + (unsigned)d;
        ++cursor;
      }
      if (cursor == digits) break;
      if (overflow) { value = UINTMAX_MAX; errno = ERANGE; }
      else if (negative) value = 0 - value;
      text = cursor;
      if (out) {
        if (specifier == 'p') *(void **)out = (void *)(uintptr_t)value;
        else store_integer(out, length, value, specifier == 'd' || specifier == 'i');
      }
    } else goto invalid;
    converted = 1;
    if (out) ++assigned;
  }
  va_end(args);
  return assigned;
input_failure:
  va_end(args);
  return converted ? assigned : EOF;
invalid:
  errno = EINVAL;
  va_end(args);
  return assigned ? assigned : EOF;
}

int sscanf(const char *input, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsscanf(input, format, args);
  va_end(args);
  return result;
}
