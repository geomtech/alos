#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

static int failures;
#define EXPECT(condition) do { if (!(condition)) { \
    printf("vector-io-test: FAIL line %d errno=%d\n", __LINE__, errno); \
    ++failures; } } while (0)

static void basic(void) {
    int fds[2];
    EXPECT(pipe(fds) == 0);
    char a[2] = {0}, b[8] = {0};
    struct iovec output[] = {{NULL, 0}, {"ab", 2}, {"cdef", 4}};
    struct iovec input[] = {{a, 2}, {NULL, 0}, {b, sizeof(b)}};
    EXPECT(writev(fds[1], output, 3) == 6);
    int available = -1;
    EXPECT(ioctl(fds[0], FIONREAD, &available) == 0 && available == 6);
    EXPECT(readv(fds[0], input, 3) == 6);
    EXPECT(!memcmp(a, "ab", 2) && !memcmp(b, "cdef", 4));
    EXPECT(ioctl(fds[0], FIONREAD, &available) == 0 && available == 0);
    EXPECT(ioctl(fds[0], FIONREAD, (void *)(uintptr_t)1) == -1 && errno == EFAULT);
    EXPECT(ioctl(fds[0], 0xdeadUL) == -1 && errno == ENOTTY);
    /* Writer stays open: a positive first vector must not wait for the next. */
    EXPECT(write(fds[1], "xy", 2) == 2);
    EXPECT(readv(fds[0], input, 3) == 2);
    EXPECT(!memcmp(a, "xy", 2));
    EXPECT(readv(fds[0], NULL, 0) == 0);
    EXPECT(writev(fds[1], NULL, 0) == 0);
    EXPECT(readv(fds[0], input, -1) == -1 && errno == EINVAL);
    EXPECT(writev(fds[1], output, IOV_MAX + 1) == -1 && errno == EINVAL);
    EXPECT(readv(fds[0], (void *)(uintptr_t)1, 1) == -1 && errno == EFAULT);
    struct iovec invalid[] = {{"safe", 4}, {(void *)(uintptr_t)1, 1}};
    EXPECT(writev(fds[1], invalid, 2) == -1 && errno == EFAULT);
    EXPECT(ioctl(fds[0], FIONREAD, &available) == 0 && available == 0);
    struct iovec overflow[] = {{a, (size_t)INT64_MAX}, {b, 1}};
    EXPECT(writev(fds[1], overflow, 2) == -1 && errno == EINVAL);
    struct iovec zero = {(void *)(uintptr_t)1, 0};
    EXPECT(readv(fds[0], &zero, 1) == 0);
    EXPECT(readv(fds[1], &zero, 1) == -1 && errno == EBADF);
    EXPECT(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
    EXPECT(readv(fds[0], input, 3) == -1 && errno == EAGAIN);
    close(fds[1]);
    EXPECT(readv(fds[0], input, 3) == 0);
    close(fds[0]);
    EXPECT(ioctl(fds[0], FIONREAD, &available) == -1 && errno == EBADF);
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT(socket_fd >= 0);
    EXPECT(ioctl(socket_fd, FIONREAD, &available) == 0 && available == 0);
    close(socket_fd);
}

static void nonblocking_atomic(void) {
    int fds[2];
    EXPECT(pipe2(fds, O_NONBLOCK) == 0);
    char bytes[4096];
    memset(bytes, 'p', sizeof(bytes));
    EXPECT(write(fds[1], bytes, 4095) == 4095);
    struct iovec output[] = {{"a", 1}, {"b", 1}};
    EXPECT(writev(fds[1], output, 2) == -1 && errno == EAGAIN);
    int available;
    EXPECT(ioctl(fds[0], FIONREAD, &available) == 0 && available == 4095);
    EXPECT(read(fds[0], bytes, sizeof(bytes)) == 4095);
    struct iovec large[] = {{bytes, sizeof(bytes)}, {bytes, sizeof(bytes)}};
    EXPECT(writev(fds[1], large, 2) == 4096);
    EXPECT(ioctl(fds[0], FIONREAD, &available) == 0 && available == 4096);
    close(fds[0]);
    EXPECT(writev(fds[1], output, 2) == -1 && errno == EPIPE);
    close(fds[1]);
}

struct producer { int fd; char marker; int failed; };
static void *produce(void *argument) {
    struct producer *p = argument;
    char a[31], b[33];
    memset(a, p->marker, sizeof(a));
    memset(b, p->marker, sizeof(b));
    struct iovec v[] = {{a, sizeof(a)}, {b, sizeof(b)}};
    for (int i = 0; i < 128; ++i)
        if (writev(p->fd, v, 2) != 64) { p->failed = 1; break; }
    return NULL;
}

static void concurrent_atomic(void) {
    int fds[2];
    EXPECT(pipe(fds) == 0);
    struct producer a = {fds[1], 'a', 0}, b = {fds[1], 'b', 0};
    pthread_t first, second;
    int r1 = pthread_create(&first, NULL, produce, &a);
    int r2 = pthread_create(&second, NULL, produce, &b);
    EXPECT(r1 == 0 && r2 == 0);
    if (r1 || r2) return;
    unsigned total = 0, records_a = 0, records_b = 0;
    char record[64];
    while (total < 256) {
        unsigned done = 0;
        while (done < sizeof(record)) {
            struct iovec v = {record + done, sizeof(record) - done};
            ssize_t result = readv(fds[0], &v, 1);
            EXPECT(result > 0);
            if (result <= 0) return;
            done += result;
        }
        EXPECT(record[0] == 'a' || record[0] == 'b');
        for (unsigned i = 1; i < sizeof(record); ++i)
            EXPECT(record[i] == record[0]);
        if (record[0] == 'a') ++records_a; else ++records_b;
        ++total;
    }
    EXPECT(pthread_join(first, NULL) == 0);
    EXPECT(pthread_join(second, NULL) == 0);
    EXPECT(!a.failed && !b.failed && records_a == 128 && records_b == 128);
    close(fds[0]); close(fds[1]);
}

int main(void) {
    basic();
    nonblocking_atomic();
    concurrent_atomic();
    printf("vector-io-test: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
