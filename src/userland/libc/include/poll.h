#ifndef _POLL_H
#define _POLL_H
#include "../../../include/native_socket.h"
typedef unsigned long nfds_t;
struct pollfd { int fd; short events, revents; };
#define POLLIN ALOS_POLLIN
#define POLLPRI 2
#define POLLOUT ALOS_POLLOUT
#define POLLERR ALOS_POLLERR
#define POLLHUP ALOS_POLLHUP
#define POLLNVAL ALOS_POLLNVAL
#define POLLRDNORM 0x40
#define POLLWRNORM 0x100
#ifdef __cplusplus
extern "C" {
#endif
int poll(struct pollfd *, nfds_t, int);
#ifdef __cplusplus
}
#endif
#endif
