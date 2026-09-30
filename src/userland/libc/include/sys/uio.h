#ifndef _SYS_UIO_H
#define _SYS_UIO_H
#include <sys/types.h>
#include <stddef.h>
#include "../../../../include/native_io.h"
#define IOV_MAX ALOS_IOV_MAX
#define UIO_MAXIOV IOV_MAX
struct iovec { void *iov_base; size_t iov_len; };
#ifdef __cplusplus
extern "C" {
#endif
ssize_t readv(int fd, const struct iovec *vectors, int count);
ssize_t writev(int fd, const struct iovec *vectors, int count);
#ifdef __cplusplus
}
#endif
#endif
