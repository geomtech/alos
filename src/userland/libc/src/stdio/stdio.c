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
      int n = va_arg(ap, int);
      char buf[32];
      itoa(n, buf, 10);
      char *b = buf;
      while (*b) {
        if (ptr < end)
          *ptr++ = *b;
        b++;
      }
      break;
    }
    case 'x':
    case 'X': {
      unsigned int n = va_arg(ap, unsigned int);
      char buf[32];
      itoa(n, buf, 16);
      char *b = buf;
      while (*b) {
        if (ptr < end)
          *ptr++ = *b;
        b++;
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
