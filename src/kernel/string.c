/* src/kernel/string.c - String manipulation functions */
#include "../include/string.h"

/* Calcule la longueur d'une chaîne */
size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len])
    len++;
  return len;
}

/* Compare deux chaînes */
int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Compare les n premiers caractères de deux chaînes */
int strncmp(const char *s1, const char *s2, size_t n) {
  while (n > 0 && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Copie une chaîne */
char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

/* Copie au maximum n caractères */
char *strncpy(char *dest, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];
  for (; i < n; i++)
    dest[i] = '\0';
  return dest;
}

/* Concatène deux chaînes */
char *strcat(char *dest, const char *src) {
  char *d = dest;
  while (*d)
    d++;
  while ((*d++ = *src++))
    ;
  return dest;
}

/* Tokenize une chaîne (réentrante) */
char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *token;

  if (str == NULL)
    str = *saveptr;

  /* Skip leading delimiters */
  while (*str && strchr(delim, *str)) {
    str++;
  }

  if (*str == '\0') {
    *saveptr = str;
    return NULL;
  }

  token = str;

  /* Find end of token */
  while (*str && !strchr(delim, *str)) {
    str++;
  }

  if (*str) {
    *str++ = '\0';
    *saveptr = str;
  } else {
    *saveptr = str;
  }

  return token;
}

/* Non-reentrant strtok */
static char *strtok_static = NULL;
char *strtok(char *str, const char *delim) {
  return strtok_r(str, delim, &strtok_static);
}

/* Helper for strtok: strchr */
char *strchr(const char *str, int c) {
  while (*str != (char)c) {
    if (!*str++) {
      return NULL;
    }
  }
  return (char *)str;
}

/* Helper for strtok: strrchr */
char *strrchr(const char *str, int c) {
  const char *last = NULL;
  do {
    if (*str == (char)c)
      last = str;
  } while (*str++);
  return (char *)last;
}

/* Helper: strstr */
char *strstr(const char *haystack, const char *needle) {
  size_t n = strlen(needle);
  while (*haystack) {
    if (!memcmp(haystack, needle, n))
      return (char *)haystack;
    haystack++;
  }
  return NULL;
}

/* Memset */
void *memset(void *ptr, int value, size_t n) {
  unsigned char *p = (unsigned char *)ptr;
  while (n--)
    *p++ = (unsigned char)value;
  return ptr;
}

/* Memcpy */
void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  while (n--)
    *d++ = *s++;
  return dest;
}

/* Memcmp */
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

/* Memmove */
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

/* Memchr */
void *memchr(const void *ptr, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)ptr;
  while (n--) {
    if (*p == (unsigned char)c)
      return (void *)p;
    p++;
  }
  return NULL;
}

/* strdup */
#include "../mm/kheap.h" // For kmalloc
char *strdup(const char *str) {
  size_t len = strlen(str) + 1;
  char *copy = (char *)kmalloc(len);
  if (copy)
    memcpy(copy, str, len);
  return copy;
}

/* atoi */
int atoi(const char *str) {
  int res = 0;
  int sign = 1;
  while (*str == ' ' || *str == '\t' || *str == '\n')
    str++; // Skip whitespace

  if (*str == '-') {
    sign = -1;
    str++;
  } else if (*str == '+') {
    str++;
  }

  while (*str >= '0' && *str <= '9') {
    res = res * 10 + (*str - '0');
    str++;
  }
  return sign * res;
}

/* isspace etc */
int isspace(int c) {
  return c == ' ' || c == '\n' || c == '\t' || c == '\v' || c == '\f' ||
         c == '\r';
}

int isdigit(int c) { return c >= '0' && c <= '9'; }

int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* strncat stub if needed */
char *strncat(char *dest, const char *src, size_t n) {
  char *d = dest;
  while (*d)
    d++;
  while (n-- && *src)
    *d++ = *src++;
  *d = '\0';
  return dest;
}
