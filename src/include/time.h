#ifndef ALOS_TIME_ABI_H
#define ALOS_TIME_ABI_H
#include <stdint.h>
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME 2
#define CLOCK_MONOTONIC_RAW 3
#define CLOCK_MONOTONIC_COARSE 4
#define CLOCK_THREAD_CPUTIME_ID 5
#define CLOCK_REALTIME_COARSE 6
typedef int clockid_t;
struct timespec { int64_t tv_sec; int64_t tv_nsec; };
#endif
