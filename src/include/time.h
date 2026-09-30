#ifndef ALOS_TIME_ABI_H
#define ALOS_TIME_ABI_H
#include <stdint.h>
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
typedef int clockid_t;
struct timespec { int64_t tv_sec; int64_t tv_nsec; };
#endif
