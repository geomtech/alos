#ifndef ALOS_NATIVE_IO_H
#define ALOS_NATIVE_IO_H
#include <stdint.h>
#include <stddef.h>
#define ALOS_SYS_READV 270
#define ALOS_SYS_WRITEV 271
#define ALOS_SYS_IOCTL 272
#define ALOS_IOV_MAX 1024
#define ALOS_FIONREAD 0x541B
typedef struct alos_iovec {
    void *iov_base;
    size_t iov_len;
} alos_iovec_t;
#endif
