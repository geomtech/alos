#ifndef ALOS_RESOURCE_ABI_H
#define ALOS_RESOURCE_ABI_H

#include <stdint.h>

#define ALOS_RESOURCE_NOFILE 1
#define ALOS_RESOURCE_DATA 2
#define ALOS_RESOURCE_INFINITY UINT64_MAX

typedef struct {
  uint64_t current;
  uint64_t maximum;
} alos_resource_limit_t;

#endif
