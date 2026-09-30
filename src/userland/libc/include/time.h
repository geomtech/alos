#ifndef _TIME_H
#define _TIME_H
#include "../../../include/time.h"
#include <sys/types.h>
#include <stddef.h>
#include <bits/alos_wchar.h>
struct tm {
  int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
  int tm_wday, tm_yday, tm_isdst;
  long tm_gmtoff;
  const char *tm_zone;
};
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t clock, struct timespec *time);
int nanosleep(const struct timespec *request, struct timespec *remaining);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *, struct tm *);
time_t timegm(struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);
size_t strftime_l(char *, size_t, const char *, const struct tm *, locale_t);
time_t time(time_t *);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *);
time_t mktime(struct tm *);
char *strptime(const char *, const char *, struct tm *);
/* Profil UTC fixe : TZ non pris en charge donne errno=ENOTSUP. */
void tzset(void);
extern char *tzname[2];
extern long timezone;
extern int daylight;
#ifdef __cplusplus
}
#endif
#endif
