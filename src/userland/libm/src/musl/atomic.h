#ifndef _ALOS_MUSL_ATOMIC_H
#define _ALOS_MUSL_ATOMIC_H
#include <stdint.h>
static inline int a_clz_64(uint64_t x) { return __builtin_clzll(x); }
#endif

