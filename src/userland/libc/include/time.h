#ifndef _TIME_H
#define _TIME_H
#include "../../../include/time.h"
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t clock, struct timespec *time);
int nanosleep(const struct timespec *request, struct timespec *remaining);
#ifdef __cplusplus
}
#endif
#endif
