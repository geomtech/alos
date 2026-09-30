#ifndef ALOS_SYS_TIME_H
#define ALOS_SYS_TIME_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long suseconds_t;
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };
int gettimeofday(struct timeval *, void *);
int futimes(int fd, const struct timeval times[2]);
#ifdef __cplusplus
}
#endif
#endif
