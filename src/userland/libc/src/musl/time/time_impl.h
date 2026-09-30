/* Adaptation ALOS des routines musl src/time (voir COPYRIGHT.musl). */
#ifndef ALOS_MUSL_TIME_IMPL_H
#define ALOS_MUSL_TIME_IMPL_H
#include <time.h>
#include <locale.h>
#define __tm_gmtoff tm_gmtoff
#define __tm_zone tm_zone
int __month_to_secs(int, int);
long long __year_to_secs(long long, int *);
long long __tm_to_secs(const struct tm *);
int __secs_to_tm(long long, struct tm *);
size_t __strftime_l(char *, size_t, const char *, const struct tm *, locale_t);
extern const char __utc[];
static inline const char *__tm_to_tzname(const struct tm *value) {
  return value->tm_zone ? value->tm_zone : "";
}
#endif
