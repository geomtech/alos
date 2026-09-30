#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

static int failures;
#define CHECK(x) do { if (!(x)) {     printf("epoll-test FAIL line %d: %s errno=%d\n", __LINE__, #x, errno);     ++failures; } } while (0)

static void drain(int fd) {
    char buffer[32];
    while (read(fd, buffer, sizeof(buffer)) > 0) {}
    if (errno == EAGAIN) errno = 0;
}

int main(void) {
    errno = 0;
    CHECK(epoll_create(0) == -1 && errno == EINVAL);

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    CHECK(epfd >= 0);
    if (epfd < 0) return 1;
    CHECK((fcntl(epfd, F_GETFD, 0) & FD_CLOEXEC) != 0);

    errno = 0;
    CHECK(epoll_create1(0x4000) == -1 && errno == EINVAL);

    int pipefd[2] = {-1, -1};
    CHECK(pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) == 0);

    struct epoll_event event = {0};
    event.events = EPOLLIN;
    event.data.u64 = UINT64_C(0x1122334455667788);
    CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &event) == 0);

    errno = 0;
    CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, pipefd[0], &event) == -1 &&
          errno == EEXIST);
    errno = 0;
    CHECK(epoll_ctl(epfd, EPOLL_CTL_ADD, epfd, &event) == -1 &&
          errno == EINVAL);

    struct epoll_event ready[4];
    CHECK(epoll_wait(epfd, ready, 4, 0) == 0);

    char byte = 'A';
    CHECK(write(pipefd[1], &byte, 1) == 1);
    CHECK(epoll_wait(epfd, ready, 4, 1000) == 1);
    CHECK((ready[0].events & EPOLLIN) != 0);
    CHECK(ready[0].data.u64 == UINT64_C(0x1122334455667788));

    /* Level-triggered readiness remains visible until the pipe is drained. */
    CHECK(epoll_wait(epfd, ready, 4, 0) == 1);
    CHECK(read(pipefd[0], &byte, 1) == 1);
    CHECK(epoll_wait(epfd, ready, 4, 0) == 0);

    /* Edge-triggered readiness is reported once for the same ready state. */
    event.events = EPOLLIN | EPOLLET;
    event.data.u64 = 7;
    CHECK(epoll_ctl(epfd, EPOLL_CTL_MOD, pipefd[0], &event) == 0);
    CHECK(write(pipefd[1], "BC", 2) == 2);
    CHECK(epoll_wait(epfd, ready, 4, 1000) == 1);
    CHECK(ready[0].data.u64 == 7);
    CHECK(epoll_wait(epfd, ready, 4, 0) == 0);
    drain(pipefd[0]);
    CHECK(epoll_wait(epfd, ready, 4, 0) == 0);
    CHECK(write(pipefd[1], "D", 1) == 1);
    CHECK(epoll_wait(epfd, ready, 4, 1000) == 1);
    drain(pipefd[0]);

    /* One-shot disables the watch until EPOLL_CTL_MOD rearms it. */
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.u64 = 9;
    CHECK(epoll_ctl(epfd, EPOLL_CTL_MOD, pipefd[0], &event) == 0);
    CHECK(write(pipefd[1], "E", 1) == 1);
    CHECK(epoll_wait(epfd, ready, 4, 1000) == 1);
    CHECK(epoll_wait(epfd, ready, 4, 0) == 0);
    CHECK(epoll_ctl(epfd, EPOLL_CTL_MOD, pipefd[0], &event) == 0);
    CHECK(epoll_wait(epfd, ready, 4, 0) == 1);
    drain(pipefd[0]);

    CHECK(epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], NULL) == 0);
    errno = 0;
    CHECK(epoll_ctl(epfd, EPOLL_CTL_DEL, pipefd[0], NULL) == -1 &&
          errno == ENOENT);

    CHECK(write(pipefd[1], "F", 1) == 1);
    CHECK(epoll_wait(epfd, ready, 4, 0) == 0);

    errno = 0;
    CHECK(epoll_wait(epfd, ready, 0, 0) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(epoll_pwait(epfd, ready, 4, 0, (const void *)1) == -1 &&
          errno == ENOTSUP);

    close(pipefd[0]);
    close(pipefd[1]);
    close(epfd);

    printf("epoll-test: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
