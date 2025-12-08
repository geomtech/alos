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

void *memset(void *ptr, int value, size_t n) {
  uint8_t *p = (uint8_t *)ptr;
  while (n--)
    *p++ = (uint8_t)value;
  return ptr;
}

void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  while (n--)
    *d++ = *s++;
  return dest;
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
