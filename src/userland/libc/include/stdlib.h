#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <bits/alos_wchar.h>

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
int atexit(void (*function)(void));
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
long atol(const char *nptr);
char *itoa(int value, char *str, int base);
/* Pointeur emprunte : synchroniser son usage avec toute mutation de sa variable.
 * L'acces direct a environ et execve(environ) exigent aussi cette synchronisation. */
char *getenv(const char *name);
char *realpath(const char *path, char *resolved);
int mkstemp(char *pattern);
char *mkdtemp(char *pattern);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
extern char **environ;
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
float strtof(const char *nptr, char **endptr);
double strtod(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
float strtof_l(const char *, char **, locale_t);
double strtod_l(const char *, char **, locale_t);
long double strtold_l(const char *, char **, locale_t);
#define MB_CUR_MAX ((size_t)4)
size_t __ctype_get_mb_cur_max(void);
int mblen(const char *, size_t);
int mbtowc(wchar_t *__restrict, const char *__restrict, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *__restrict, const char *__restrict, size_t);
size_t wcstombs(char *__restrict, const wchar_t *__restrict, size_t);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t,
              int (*)(const void *, const void *));


#ifdef __cplusplus
}
#endif
#endif
