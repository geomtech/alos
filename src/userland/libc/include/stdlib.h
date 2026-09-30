#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **result, size_t alignment, size_t size);

void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
int atoi(const char *nptr);
char *itoa(int value, char *str, int base);
char *getenv(const char *name);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
float strtof(const char *nptr, char **endptr);
double strtod(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
#define MB_CUR_MAX ((size_t)4)
size_t __ctype_get_mb_cur_max(void);
int mblen(const char *, size_t);
int mbtowc(__WCHAR_TYPE__ *__restrict, const char *__restrict, size_t);
int wctomb(char *, __WCHAR_TYPE__);
size_t mbstowcs(__WCHAR_TYPE__ *__restrict, const char *__restrict, size_t);
size_t wcstombs(char *__restrict, const __WCHAR_TYPE__ *__restrict, size_t);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));


#ifdef __cplusplus
}
#endif
#endif
