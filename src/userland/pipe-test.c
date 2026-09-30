#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
extern int pipe(int[2]);
extern int pipe2(int[2], int);
struct pipe_pollfd { int fd; short events, revents; };
extern int poll(struct pipe_pollfd*, unsigned long, int);
static int failures;
#define CHECK(x) do { if (!(x)) { printf("pipe-test FAIL %d: %s errno=%d\n", __LINE__, #x, errno); failures++; } } while (0)

static void* blocked_writer(void* argument) {
    int fd = *(int*)argument;
    write(fd, "z", 1);
    return NULL;
}

static void group_exit(int status) {
    syscall1(SYS_EXIT_GROUP, status);
    __builtin_unreachable();
}

int main(void) {
    int fds[2];
    CHECK(pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0);
    CHECK(fcntl(fds[0], F_GETFD) == FD_CLOEXEC);
    struct pipe_pollfd ready[2] = {{fds[0], 1, 0}, {fds[1], 4, 0}};
    CHECK(poll(ready, 2, 0) == 1 && ready[0].revents == 0 &&
          (ready[1].revents & 4));
    char c;
    CHECK(read(fds[0], &c, 1) == -1 && errno == EAGAIN);
    CHECK(write(fds[0], "x", 1) == -1 && errno == EBADF);
    CHECK(read(fds[1], &c, 1) == -1 && errno == EBADF);
    char bytes[4096];
    memset(bytes, 'q', sizeof(bytes));
    CHECK(write(fds[1], bytes, sizeof(bytes)) == sizeof(bytes));
    CHECK(write(fds[1], "x", 1) == -1 && errno == EAGAIN);
    CHECK(read(fds[0], bytes, sizeof(bytes)) == sizeof(bytes));
    int duplicate = dup(fds[1]);
    CHECK(duplicate >= 0);
    CHECK(close(fds[1]) == 0);
    CHECK(write(duplicate, "a", 1) == 1);
    CHECK(close(duplicate) == 0);
    ready[0].revents = 0;
    CHECK(poll(ready, 1, 0) == 1 && (ready[0].revents & 1) &&
          (ready[0].revents & 16));
    CHECK(read(fds[0], &c, 1) == 1 && c == 'a');
    CHECK(read(fds[0], &c, 1) == 0);
    CHECK(close(fds[0]) == 0);
    CHECK(pipe(fds) == 0);
    int child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(fds[0]);
        usleep(20000);
        int ok = write(fds[1], "b", 1) == 1;
        close(fds[1]);
        _exit(ok ? 0 : 1);
    }
    close(fds[1]);
    int filled[32], number = 0;
    while (number < 32) {
        int fd = dup(0);
        if (fd < 0) break;
        filled[number++] = fd;
    }
    CHECK(number > 0 && errno == EMFILE);
    if (number) {
        close(filled[--number]);
        CHECK(pipe(fds) == -1 && errno == EMFILE);
        int spare = dup(0);
        CHECK(spare >= 0);
        if (spare >= 0) close(spare);
    }
    while (number) close(filled[--number]);
    struct pipe_pollfd delayed = {fds[0], 1, 0};
    CHECK(poll(&delayed, 1, 500) == 1 && (delayed.revents & 1));
    CHECK(read(fds[0], &c, 1) == 1 && c == 'b');
    CHECK(read(fds[0], &c, 1) == 0);
    int status = -1;
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    close(fds[0]);
    CHECK(pipe(fds) == 0);
    close(fds[0]);
    struct pipe_pollfd broken = {fds[1], 4, 0};
    CHECK(poll(&broken, 1, 0) == 1 && (broken.revents & 8));
    CHECK(write(fds[1], "x", 1) == -1 && errno == EPIPE);
    close(fds[1]);
    CHECK(pipe(fds) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(fds[0]);
        if (write(fds[1], bytes, sizeof(bytes)) != sizeof(bytes)) group_exit(1);
        pthread_t thread;
        if (pthread_create(&thread, NULL, blocked_writer, &fds[1])) group_exit(1);
        usleep(50000);
        group_exit(0);
    }
    close(fds[1]);
    status = -1;
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(read(fds[0], bytes, sizeof(bytes)) == sizeof(bytes));
    CHECK(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
    /* Ne pas bloquer indefiniment si un writer du syscall abandonne fuit. */
    CHECK(read(fds[0], &c, 1) == 0);
    close(fds[0]);
    printf("pipe-test: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
