#ifndef _UTIME_H
#define _UTIME_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct utimbuf {
  time_t actime;
  time_t modtime;
};
/* Implemente par utimes() (SYS_FUTIMES) ; NULL = heure courante. */
int utime(const char *path, const struct utimbuf *times);
#ifdef __cplusplus
}
#endif
#endif
