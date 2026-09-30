#include <errno.h>
#include <stddef.h>
#include "../../../../include/entropy_abi.h"
#include "internal/syscall.h"

int getentropy(void *buffer, size_t length) {
    long result = syscall2(ALOS_SYS_GETENTROPY, (long)buffer, (long)length);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}
