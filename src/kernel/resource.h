#ifndef KERNEL_RESOURCE_H
#define KERNEL_RESOURCE_H

#include "../include/resource_abi.h"

int sys_resource_limit(int resource, alos_resource_limit_t *destination);
int sys_resource_set_limit(int resource, const alos_resource_limit_t *source);
int sys_thread_nice(int operation, int value, int *destination);

#endif
