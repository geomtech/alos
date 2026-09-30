#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
const char __utc[] = "UTC";
static char utc_name[] = "UTC";
char *tzname[2] = {utc_name, utc_name};
long timezone = 0;
int daylight = 0;

/* Aucun chargement de TZ/DST ; conserver UTC et signaler les autres profils. */
void tzset(void) {
  const char *zone = getenv("TZ");
  if (!zone || !*zone) return;
  if (*zone == ':') zone++;
  if (!strcmp(zone, "UTC") || !strcmp(zone, "GMT") ||
      !strcmp(zone, "UTC0") || !strcmp(zone, "GMT0")) return;
  errno = ENOTSUP;
}

struct tm *gmtime(const time_t *value) {
  static struct tm result;
  return gmtime_r(value, &result);
}
/* Le fuseau local ALOS est UTC ; aucune base de zones ou DST n'est active. */
struct tm *localtime_r(const time_t *value, struct tm *result) {
  return gmtime_r(value, result);
}
struct tm *localtime(const time_t *value) { return gmtime(value); }
time_t mktime(struct tm *value) { return timegm(value); }
time_t time(time_t *out) {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now)) return (time_t)-1;
  if (out) *out = now.tv_sec;
  return now.tv_sec;
}
