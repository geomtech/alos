#ifndef KERNEL_SYSTEM_INFO_H
#define KERNEL_SYSTEM_INFO_H

#include <stddef.h>
#include "../include/system_info.h"

int sys_system_info(alos_system_info_t *destination, size_t size,
                    unsigned version);

#endif
