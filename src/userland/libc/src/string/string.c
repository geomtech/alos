/* src/userland/libc/src/string/string.c - String utilities implementation */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len]) {
    len++;
  }
  return len;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0) {
    return 0;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
  char *ret = dest;
  while ((*dest++ = *src++))
    ;
  return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
  char *ret = dest;
  while (n && (*dest++ = *src++)) {
    n--;
  }
  while (n--) {
    *dest++ = '\0';
  }
  return ret;
}

char *strcat(char *dest, const char *src) {
  char *ret = dest;
  while (*dest) {
    dest++;
  }
  while ((*dest++ = *src++))
    ;
  return ret;
}

char *strncat(char *dest, const char *src, size_t n) {
  char *ret = dest;
  while (*dest)
    dest++;
  while (n && (*dest++ = *src++))
    n--;
  *dest = '\0';
  return ret;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *token_start;
  if (str == NULL) {
    str = *saveptr;
  }
  if (str == NULL) {
    return NULL;
  }
  while (*str) {
    if (strchr(delim, *str) == NULL) {
      break;
    }
    str++;
  }
  if (*str == '\0') {
    *saveptr = NULL;
    return NULL;
  }
  token_start = str;
  while (*str) {
    if (strchr(delim, *str) != NULL) {
      *str = '\0';
      *saveptr = str + 1;
      return token_start;
    }
    str++;
  }
  *saveptr = NULL;
  return token_start;
}

char *strtok(char *str, const char *delim) {
  static char *saveptr = NULL;
  return strtok_r(str, delim, &saveptr);
}

/* Cache CPU feature detection: ERMS = Enhanced REP MOVSB/STOSB. */
static int cpu_has_erms(void) {
  static int cached = -1;
  if (cached >= 0)
    return cached;

  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0), "c"(0));

  if (eax < 7) {
    cached = 0;
    return cached;
  }

  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(7), "c"(0));
  cached = (ebx & (1u << 9)) ? 1 : 0;
  return cached;
}

void *memset(void *ptr, int value, size_t n) {
  void *ret = ptr;
  uint8_t *dst = (uint8_t *)ptr;

  if (n >= 64 && cpu_has_erms()) {
    size_t count = n;
    __asm__ volatile("cld; rep stosb"
                     : "+D"(dst), "+c"(count)
                     : "a"((uint8_t)value)
                     : "memory", "cc");
    return ret;
  }

  if (n >= 16) {
    uint64_t byte = (uint8_t)value;
    uint64_t pattern = byte * 0x0101010101010101ULL;
    uint64_t *dst64 = (uint64_t *)dst;
    size_t qwords = n / 8;
    __asm__ volatile("cld; rep stosq"
                     : "+D"(dst64), "+c"(qwords)
                     : "a"(pattern)
                     : "memory", "cc");
    dst = (uint8_t *)dst64;
    n &= 7;
  }

  while (n--)
    *dst++ = (uint8_t)value;
  return ret;
}

void *memcpy(void *dest, const void *src, size_t n) {
  void *ret = dest;
  uint8_t *dst = (uint8_t *)dest;
  const uint8_t *source = (const uint8_t *)src;

  if (n >= 64 && cpu_has_erms()) {
    size_t count = n;
    __asm__ volatile("cld; rep movsb"
                     : "+D"(dst), "+S"(source), "+c"(count)
                     :
                     : "memory", "cc");
    return ret;
  }

  if (n >= 16) {
    uint64_t *dst64 = (uint64_t *)dst;
    const uint64_t *src64 = (const uint64_t *)source;
    size_t qwords = n / 8;
    __asm__ volatile("cld; rep movsq"
                     : "+D"(dst64), "+S"(src64), "+c"(qwords)
                     :
                     : "memory", "cc");
    dst = (uint8_t *)dst64;
    source = (const uint8_t *)src64;
    n &= 7;
  }

  while (n--)
    *dst++ = *source++;
  return ret;
}

void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  if (d < s) {
    while (n--)
      *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }
  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  while (n--) {
    if (*p1 != *p2)
      return *p1 - *p2;
    p1++;
    p2++;
  }
  return 0;
}

char *strchr(const char *str, int c) {
  while (*str != '\0') {
    if (*str == (char)c)
      return (char *)str;
    str++;
  }
  return NULL;
}

char *strrchr(const char *str, int c) {
  char *last = NULL;
  while (*str != '\0') {
    if (*str == (char)c) {
      last = (char *)str;
    }
    str++;
  }
  if ((char)c == '\0') {
    return (char *)str;
  }
  return last;
}

char *strstr(const char *haystack, const char *needle) {
  if (*needle == '\0')
    return (char *)haystack;
  while (*haystack != '\0') {
    const char *h = haystack;
    const char *n = needle;
    while (*n != '\0' && *h == *n) {
      h++;
      n++;
    }
    if (*n == '\0')
      return (char *)haystack;
    haystack++;
  }
  return NULL;
}

char *strdup(const char *str) {
  size_t len = strlen(str) + 1;
  char *new_str = malloc(len);
  if (new_str) {
    memcpy(new_str, str, len);
  }
  return new_str;
}

void *memchr(const void *ptr, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)ptr;
  while (n--) {
    if (*p == (unsigned char)c)
      return (void *)p;
    p++;
  }
  return NULL;
}
