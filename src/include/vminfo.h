#ifndef ALOS_VMINFO_H
#define ALOS_VMINFO_H

#include <stdint.h>

typedef struct {
  uint64_t physical_total;
  uint64_t physical_free;
  uint64_t virtual_size;
  uint64_t resident_size;
} vm_info_t;

#endif
