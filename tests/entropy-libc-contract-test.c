#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include "../src/include/entropy_abi.h"

int getentropy(void *, size_t);
static long answer;
static void *expected_buffer;
static size_t expected_length;
long syscall2(long number, long buffer, long length) {
    assert(number == ALOS_SYS_GETENTROPY);
    assert((void *)buffer == expected_buffer);
    assert((size_t)length == expected_length);
    return answer;
}
int main(void) {
    unsigned char bytes[256];
    expected_buffer = bytes;
    expected_length = sizeof(bytes);
    answer = -ENOSYS;
    assert(getentropy(bytes, sizeof(bytes)) == -1 && errno == ENOSYS);
    answer = -EFAULT;
    assert(getentropy(bytes, sizeof(bytes)) == -1 && errno == EFAULT);
    answer = -EIO;
    assert(getentropy(bytes, sizeof(bytes)) == -1 && errno == EIO);
    answer = 0;
    errno = 123;
    assert(getentropy(bytes, sizeof(bytes)) == 0 && errno == 123);
    expected_buffer = NULL;
    expected_length = 0;
    assert(getentropy(NULL, 0) == 0);
    return 0;
}
