#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#include "../../../../include/native_io.h"
#define FIONREAD ALOS_FIONREAD
#define TIOCINQ FIONREAD
#ifdef __cplusplus
extern "C" {
#endif
int ioctl(int fd, unsigned long request, ...);
#ifdef __cplusplus
}
#endif
#endif
