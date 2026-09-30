#ifndef KERNEL_NATIVE_IO_H
#define KERNEL_NATIVE_IO_H
#include <stdint.h>
int64_t native_vector_io(int fd, const void *vectors, int count, int write);
int64_t native_ioctl(int fd, uint64_t request, void *argument);
#endif
