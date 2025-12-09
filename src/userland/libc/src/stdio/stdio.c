/* src/userland/libc/src/stdio/stdio.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

FILE *stdin = (FILE *)0;
FILE *stdout = (FILE *)1;
FILE *stderr = (FILE *)2;

int putchar(int c) {
  char ch = (char)c;
  if (write(STDOUT_FILENO, &ch, 1) != 1)
    return -1;
  return (unsigned char)ch;
}

int puts(const char *s) {
  if (write(STDOUT_FILENO, s, strlen(s)) < 0)
    return -1;
  if (write(STDOUT_FILENO, "\n", 1) < 0)
    return -1;
  return 1;
}

int printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[1024];
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len > 0) {
    write(STDOUT_FILENO, buffer, len);
  }
  return len;
}

int sprintf(char *str, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int len = vsprintf(str, format, args);
  va_end(args);
  return len;
}

int snprintf(char *str, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int len = vsnprintf(str, size, format, args);
  va_end(args);
  return len;
}

int vsprintf(char *str, const char *format, va_list ap) {
  return vsnprintf(str, 0x7FFFFFFF, format, ap);
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  char *ptr = str;
  char *end = str + size - 1;
  if (size == 0)
    end = str - 1;

  while (*format) {
    if (*format != '%') {
      if (ptr < end)
        *ptr++ = *format;
      format++;
      continue;
    }

    format++;

    /* Check for length modifier */
    int is_long = 0;
    if (*format == 'l') {
      is_long = 1;
      format++;
      /* Check for 'll' (long long) */
      if (*format == 'l') {
        is_long = 2;
        format++;
      }
    }

    switch (*format) {
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s)
        s = "(null)";
      while (*s) {
        if (ptr < end)
          *ptr++ = *s;
        s++;
      }
      break;
    }
    case 'd':
    case 'i': {
      long long n;
      if (is_long >= 2) {
        n = va_arg(ap, long long);
      } else if (is_long == 1) {
        n = va_arg(ap, long);
      } else {
        n = va_arg(ap, int);
      }
      char buf[32];
      /* Handle negative numbers */
      int neg = 0;
      if (n < 0) {
        neg = 1;
        n = -n;
      }
      char *bp = buf + 31;
      *bp = '\0';
      do {
        *(--bp) = '0' + (n % 10);
        n /= 10;
      } while (n);
      if (neg)
        *(--bp) = '-';
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      break;
    }
    case 'u': {
      unsigned long long n;
      if (is_long >= 2) {
        n = va_arg(ap, unsigned long long);
      } else if (is_long == 1) {
        n = va_arg(ap, unsigned long);
      } else {
        n = va_arg(ap, unsigned int);
      }
      char buf[32];
      char *bp = buf + 31;
      *bp = '\0';
      do {
        *(--bp) = '0' + (n % 10);
        n /= 10;
      } while (n);
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      break;
    }
    case 'x':
    case 'X': {
      unsigned long long n;
      if (is_long >= 2) {
        n = va_arg(ap, unsigned long long);
      } else if (is_long == 1) {
        n = va_arg(ap, unsigned long);
      } else {
        n = va_arg(ap, unsigned int);
      }
      char buf[32];
      const char *digits =
          (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
      char *bp = buf + 31;
      *bp = '\0';
      do {
        *(--bp) = digits[n & 0xF];
        n >>= 4;
      } while (n);
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      break;
    }
    case 'p': {
      /* Pointer format */
      unsigned long long n = (unsigned long long)(uintptr_t)va_arg(ap, void *);
      if (ptr < end)
        *ptr++ = '0';
      if (ptr < end)
        *ptr++ = 'x';
      char buf[32];
      char *bp = buf + 31;
      *bp = '\0';
      do {
        *(--bp) = "0123456789abcdef"[n & 0xF];
        n >>= 4;
      } while (n);
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      break;
    }
    case 'c':
      if (ptr < end)
        *ptr++ = (char)va_arg(ap, int);
      break;
    case '%':
      if (ptr < end)
        *ptr++ = '%';
      break;
    default:
      if (ptr < end)
        *ptr++ = '%';
      if (is_long && ptr < end)
        *ptr++ = 'l';
      if (ptr < end)
        *ptr++ = *format;
      break;
    }
    format++;
  }
  if (size > 0)
    *ptr = '\0';
  return ptr - str;
}
