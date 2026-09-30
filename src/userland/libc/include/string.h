#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>
#include <stdint.h>
#include <strings.h>
#ifdef __cplusplus
extern "C" {
#endif

size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strdup(const char *str);
char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

void *memset(void *ptr, int value, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *ptr, int c, size_t n);
char *strerror(int error);
int strerror_r(int error, char *buffer, size_t size);
size_t strnlen(const char *, size_t);
size_t strspn(const char *, const char *);
size_t strcspn(const char *, const char *);
char *strpbrk(const char *, const char *);
char *strndup(const char *, size_t);
char *stpcpy(char *__restrict, const char *__restrict);
char *stpncpy(char *__restrict, const char *__restrict, size_t);
char *strsep(char **, const char *);
void *memrchr(const void *, int, size_t);
void *memmem(const void *, size_t, const void *, size_t);
void *mempcpy(void *, const void *, size_t);
char *strchrnul(const char *, int);
char *strcasestr(const char *, const char *);
size_t strlcpy(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
int strcoll(const char *, const char *);
size_t strxfrm(char *__restrict, const char *__restrict, size_t);
#include <bits/alos_wchar.h>
int strcoll_l(const char *, const char *, locale_t);
size_t strxfrm_l(char *__restrict, const char *__restrict, size_t, locale_t);

#ifdef __cplusplus
}
#endif
#endif
