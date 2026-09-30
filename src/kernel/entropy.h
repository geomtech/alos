#ifndef ALOS_KERNEL_ENTROPY_H
#define ALOS_KERNEL_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

int64_t entropy_getentropy(void *user_buffer, size_t length);

#endif
