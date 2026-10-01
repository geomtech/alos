#ifndef ALOS_RESOURCE_ABI_H
#define ALOS_RESOURCE_ABI_H

#include <stdint.h>

#define ALOS_RESOURCE_NOFILE 1
#define ALOS_RESOURCE_DATA 2
#define ALOS_RESOURCE_INFINITY UINT64_MAX

#define ALOS_RUSAGE_SELF 0
#define ALOS_RUSAGE_CHILDREN 1
#define ALOS_RUSAGE_THREAD 2

typedef struct {
  uint64_t current;
  uint64_t maximum;
} alos_resource_limit_t;

typedef struct {
  int64_t tv_sec;
  int64_t tv_usec;
} alos_rusage_timeval_t;

typedef struct {
  alos_rusage_timeval_t ru_utime;
  alos_rusage_timeval_t ru_stime;
  int64_t ru_maxrss;
  int64_t ru_ixrss;
  int64_t ru_idrss;
  int64_t ru_isrss;
  int64_t ru_minflt;
  int64_t ru_majflt;
  int64_t ru_nswap;
  int64_t ru_inblock;
  int64_t ru_oublock;
  int64_t ru_msgsnd;
  int64_t ru_msgrcv;
  int64_t ru_nsignals;
  int64_t ru_nvcsw;
  int64_t ru_nivcsw;
} alos_rusage_t;

#endif
