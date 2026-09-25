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

int getchar(void) {
  char c;
  if (read(STDIN_FILENO, &c, 1) <= 0)
    return -1;
  return (unsigned char)c;
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

    format++; /* skip '%' */

    /* Flags */
    int left_align = 0;
    int zero_pad = 0;
    int show_sign = 0;
    int space_sign = 0;

    int parsing_flags = 1;
    while (parsing_flags) {
      if (*format == '-') {
        left_align = 1;
        format++;
      } else if (*format == '0') {
        zero_pad = 1;
        format++;
      } else if (*format == '+') {
        show_sign = 1;
        format++;
      } else if (*format == ' ') {
        space_sign = 1;
        format++;
      } else {
        parsing_flags = 0;
      }
    }
    if (left_align) {
      zero_pad = 0; /* '-' overrides '0' */
    }

    /* Width */
    int width = 0;
    if (*format == '*') {
      width = va_arg(ap, int);
      if (width < 0) {
        left_align = 1;
        width = -width;
      }
      format++;
    } else {
      while (*format >= '0' && *format <= '9') {
        width = width * 10 + (*format - '0');
        format++;
      }
    }

    /* Precision */
    int precision = -1;
    if (*format == '.') {
      format++;
      precision = 0;
      if (*format == '*') {
        precision = va_arg(ap, int);
        format++;
      } else {
        while (*format >= '0' && *format <= '9') {
          precision = precision * 10 + (*format - '0');
          format++;
        }
      }
    }

    /* Length modifiers */
    int is_long = 0;
    if (*format == 'l') {
      is_long = 1;
      format++;
      if (*format == 'l') {
        is_long = 2;
        format++;
      }
    } else if (*format == 'h') {
      format++;
      if (*format == 'h') {
        format++;
      }
    } else if (*format == 'z') {
      is_long = 1;
      format++;
    }

    switch (*format) {
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s)
        s = "(null)";
      int slen = 0;
      while (s[slen])
        slen++;
      if (precision >= 0 && slen > precision) {
        slen = precision;
      }
      int pad = (width > slen) ? (width - slen) : 0;
      if (!left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      for (int i = 0; i < slen; i++) {
        if (ptr < end)
          *ptr++ = s[i];
      }
      if (left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
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

      int neg = 0;
      unsigned long long un;
      if (n < 0) {
        neg = 1;
        un = (unsigned long long)(-n);
      } else {
        un = (unsigned long long)n;
      }

      char num_buf[32];
      char *bp = num_buf + sizeof(num_buf) - 1;
      *bp = '\0';
      do {
        *(--bp) = '0' + (un % 10);
        un /= 10;
      } while (un);

      int num_len = (int)(num_buf + sizeof(num_buf) - 1 - bp);
      int sign_len = (neg || show_sign || space_sign) ? 1 : 0;
      int total_len = num_len + sign_len;
      int pad = (width > total_len) ? (width - total_len) : 0;

      if (!left_align && !zero_pad) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }

      if (neg) {
        if (ptr < end)
          *ptr++ = '-';
      } else if (show_sign) {
        if (ptr < end)
          *ptr++ = '+';
      } else if (space_sign) {
        if (ptr < end)
          *ptr++ = ' ';
      }

      if (!left_align && zero_pad) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = '0';
        }
      }

      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }

      if (left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      break;
    }
    case 'u': {
      unsigned long long un;
      if (is_long >= 2) {
        un = va_arg(ap, unsigned long long);
      } else if (is_long == 1) {
        un = va_arg(ap, unsigned long);
      } else {
        un = va_arg(ap, unsigned int);
      }

      char num_buf[32];
      char *bp = num_buf + sizeof(num_buf) - 1;
      *bp = '\0';
      do {
        *(--bp) = '0' + (un % 10);
        un /= 10;
      } while (un);

      int num_len = (int)(num_buf + sizeof(num_buf) - 1 - bp);
      int pad = (width > num_len) ? (width - num_len) : 0;

      if (!left_align) {
        char pad_ch = zero_pad ? '0' : ' ';
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = pad_ch;
        }
      }
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      if (left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      break;
    }
    case 'x':
    case 'X': {
      unsigned long long un;
      if (is_long >= 2) {
        un = va_arg(ap, unsigned long long);
      } else if (is_long == 1) {
        un = va_arg(ap, unsigned long);
      } else {
        un = va_arg(ap, unsigned int);
      }

      const char *digits =
          (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
      char num_buf[32];
      char *bp = num_buf + sizeof(num_buf) - 1;
      *bp = '\0';
      do {
        *(--bp) = digits[un & 0xF];
        un >>= 4;
      } while (un);

      int num_len = (int)(num_buf + sizeof(num_buf) - 1 - bp);
      int pad = (width > num_len) ? (width - num_len) : 0;

      if (!left_align) {
        char pad_ch = zero_pad ? '0' : ' ';
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = pad_ch;
        }
      }
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      if (left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      break;
    }
    case 'p': {
      unsigned long long un = (unsigned long long)(uintptr_t)va_arg(ap, void *);
      char num_buf[32];
      char *bp = num_buf + sizeof(num_buf) - 1;
      *bp = '\0';
      do {
        *(--bp) = "0123456789abcdef"[un & 0xF];
        un >>= 4;
      } while (un);

      int num_len = (int)(num_buf + sizeof(num_buf) - 1 - bp) + 2;
      int pad = (width > num_len) ? (width - num_len) : 0;
      if (!left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      if (ptr < end)
        *ptr++ = '0';
      if (ptr < end)
        *ptr++ = 'x';
      while (*bp) {
        if (ptr < end)
          *ptr++ = *bp;
        bp++;
      }
      if (left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      break;
    }
    case 'c': {
      char c = (char)va_arg(ap, int);
      int pad = (width > 1) ? (width - 1) : 0;
      if (!left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      if (ptr < end)
        *ptr++ = c;
      if (left_align) {
        while (pad-- > 0) {
          if (ptr < end)
            *ptr++ = ' ';
        }
      }
      break;
    }
    case '%':
      if (ptr < end)
        *ptr++ = '%';
      break;
    default:
      if (ptr < end)
        *ptr++ = '%';
      if (*format && ptr < end)
        *ptr++ = *format;
      break;
    }
    format++;
  }
  if (size > 0)
    *ptr = '\0';
  return ptr - str;
}
