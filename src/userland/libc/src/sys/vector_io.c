#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdarg.h>

static long checked(long result) {
    if (result < 0) { errno = (int)-result; return -1; }
    return result;
}
ssize_t readv(int fd, const struct iovec *vectors, int count) {
    return checked(syscall3(SYS_READV, fd, (long)vectors, count));
}
ssize_t writev(int fd, const struct iovec *vectors, int count) {
    return checked(syscall3(SYS_WRITEV, fd, (long)vectors, count));
}
int ioctl(int fd, unsigned long request, ...) {
    void *argument = NULL;
    /* Seul FIONREAD consomme un argument dans le profil natif actuel. */
    if (request == FIONREAD) {
        va_list ap;
        va_start(ap, request);
        argument = va_arg(ap, void *);
        va_end(ap);
    }
    return (int)checked(syscall3(SYS_IOCTL, fd, request, (long)argument));
}
