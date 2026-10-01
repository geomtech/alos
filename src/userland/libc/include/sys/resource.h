#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include "../../../../include/resource_abi.h"

typedef uint64_t rlim_t;
struct rlimit {
  rlim_t rlim_cur;
  rlim_t rlim_max;
};

#define RLIM_INFINITY ALOS_RESOURCE_INFINITY
#define RLIMIT_NOFILE ALOS_RESOURCE_NOFILE
#define RLIMIT_DATA ALOS_RESOURCE_DATA
/* Recognized unsupported resources; these are ALOS, not Linux numbers. */
#define RLIMIT_NICE 3
#define RLIMIT_AS 4
#define RLIMIT_STACK 5
#define RLIMIT_CPU 6
#define RLIMIT_CORE 7
#define RLIMIT_RSS 8
#define RLIMIT_MEMLOCK 9
#define RLIMIT_NPROC 10
#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2

#define RUSAGE_SELF ALOS_RUSAGE_SELF
#define RUSAGE_CHILDREN ALOS_RUSAGE_CHILDREN
#define RUSAGE_THREAD ALOS_RUSAGE_THREAD

struct rusage {
  struct timeval ru_utime;
  struct timeval ru_stime;
  long ru_maxrss;
  long ru_ixrss;
  long ru_idrss;
  long ru_isrss;
  long ru_minflt;
  long ru_majflt;
  long ru_nswap;
  long ru_inblock;
  long ru_oublock;
  long ru_msgsnd;
  long ru_msgrcv;
  long ru_nsignals;
  long ru_nvcsw;
  long ru_nivcsw;
};

#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int resource, struct rlimit *limit);
int setrlimit(int resource, const struct rlimit *limit);
int getrusage(int who, struct rusage *usage);
int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int priority);
#ifdef __cplusplus
}
#endif
#endif
