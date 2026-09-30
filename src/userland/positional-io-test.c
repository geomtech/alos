#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern ssize_t pread(int, void*, size_t, off_t);
extern ssize_t pwrite(int, const void*, size_t, off_t);
extern int ftruncate(int, off_t);
extern int ftruncate64(int, int64_t);
static int failures;
#define CHECK(x) do { if (!(x)) { printf("positional-io-test FAIL %d: %s errno=%d\n", __LINE__, #x, errno); failures++; } } while (0)

struct writer { int fd; off_t offset; char value; int failed; };
static void* positional_writer(void* argument) {
    struct writer* writer = argument;
    for (unsigned i = 0; i < 100; i++)
        if (pwrite(writer->fd, &writer->value, 1, writer->offset) != 1)
            writer->failed = 1;
    return NULL;
}

int main(void) {
    char path[] = "/posix-test/positional-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return 1;
    int duplicate = dup(fd);
    CHECK(duplicate >= 0);
    CHECK(write(fd, "abcdef", 6) == 6);
    CHECK(lseek(fd, 2, SEEK_SET) == 2);
    char bytes[32] = {0};
    CHECK(pread(duplicate, bytes, 4, 1) == 4 && !memcmp(bytes, "bcde", 4));
    CHECK(lseek(fd, 0, SEEK_CUR) == 2);
    CHECK(pwrite(duplicate, "XY", 2, 4) == 2);
    CHECK(lseek(fd, 0, SEEK_CUR) == 2);
    CHECK(pread(fd, bytes, sizeof(bytes), 4) == 2 && !memcmp(bytes, "XY", 2));
    CHECK(pread(fd, bytes, 1, 6) == 0);
    CHECK(fcntl(fd, F_SETFL, O_APPEND) == 0);
    CHECK(pwrite(fd, "Z", 1, 0) == 1);
    CHECK(pread(fd, bytes, 1, 0) == 1 && bytes[0] == 'Z');
    struct stat metadata;
    CHECK(fstat(fd, &metadata) == 0 && metadata.st_size == 6);
    CHECK(fcntl(fd, F_SETFL, 0) == 0);
    CHECK(pread(fd, bytes, 1, -1) == -1 && errno == EINVAL);
    CHECK(pread(fd, bytes, 1, (off_t)UINT32_MAX + 1) == -1 && errno == EOVERFLOW);
    CHECK(pwrite(fd, "xx", 2, UINT32_MAX) == -1 && errno == EOVERFLOW);
    CHECK(lseek(fd, 0, SEEK_CUR) == 2);
    CHECK(ftruncate64(fd, 4099) == 0);
    memset(bytes, 1, sizeof(bytes));
    CHECK(pread(fd, bytes, sizeof(bytes), 6) == sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); i++) CHECK(bytes[i] == 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 2);
    CHECK(pwrite(fd, "secret", 6, 4090) == 6);
    CHECK(ftruncate(fd, 4092) == 0);
    CHECK(ftruncate(fd, 4099) == 0);
    memset(bytes, 1, sizeof(bytes));
    CHECK(pread(fd, bytes, 9, 4090) == 9 && bytes[0] == 's' && bytes[1] == 'e');
    for (unsigned i = 2; i < 9; i++) CHECK(bytes[i] == 0);
    CHECK(fstat(fd, &metadata) == 0);
    off_t block = metadata.st_blksize;
    off_t double_indirect = (12 + block / 4) * block;
    CHECK(pwrite(fd, "tail", 4, double_indirect + block) == 4);
    CHECK(fstat(fd, &metadata) == 0);
    int64_t allocated_before = metadata.st_blocks;
    CHECK(ftruncate(fd, double_indirect + block + 2) == 0);
    CHECK(ftruncate(fd, double_indirect + block + 4) == 0);
    CHECK(pread(fd, bytes, 4, double_indirect + block) == 4 &&
          bytes[0] == 't' && bytes[1] == 'a' && !bytes[2] && !bytes[3]);
    CHECK(ftruncate(fd, 3) == 0);
    CHECK(fstat(fd, &metadata) == 0 && metadata.st_size == 3 &&
          metadata.st_blocks < allocated_before);
    CHECK(pread(fd, bytes, sizeof(bytes), 0) == 3 && !memcmp(bytes, "Zbc", 3));
    CHECK(ftruncate(fd, -1) == -1 && errno == EINVAL);
    CHECK(ftruncate64(fd, (int64_t)UINT32_MAX + 1) == -1 && errno == EOVERFLOW);
    CHECK(ftruncate(fd, 0) == 0);
    CHECK(fstat(fd, &metadata) == 0 && metadata.st_size == 0 && metadata.st_blocks == 0);
    CHECK(lseek(fd, 37, SEEK_SET) == 37);
    struct writer writers[2] = {{fd, 100, 'a', 0}, {duplicate, 200, 'b', 0}};
    pthread_t threads[2];
    int started[2];
    for (unsigned i = 0; i < 2; i++) {
        started[i] = pthread_create(&threads[i], NULL, positional_writer, &writers[i]) == 0;
        CHECK(started[i]);
    }
    for (unsigned i = 0; i < 2; i++) {
        if (started[i]) CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(!writers[i].failed);
        CHECK(pread(fd, bytes, 1, writers[i].offset) == 1 && bytes[0] == writers[i].value);
    }
    CHECK(lseek(fd, 0, SEEK_CUR) == 37);
    int readonly = open(path, O_RDONLY);
    CHECK(readonly >= 0);
    CHECK(pwrite(readonly, "x", 1, 0) == -1 && errno == EBADF);
    CHECK(ftruncate(readonly, 0) == -1 && errno == EBADF);
    CHECK(close(readonly) == 0);
    int pipes[2];
    CHECK(pipe(pipes) == 0);
    CHECK(pread(pipes[0], bytes, 1, 0) == -1 && errno == ESPIPE);
    CHECK(pwrite(pipes[1], "x", 1, 0) == -1 && errno == ESPIPE);
    CHECK(ftruncate(pipes[1], 0) == -1 && errno == EINVAL);
    close(pipes[0]); close(pipes[1]);
    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    CHECK(fcntl(fd, F_SETLK, &lock) == -1 && errno == ENOTSUP);
    CHECK(fsync(fd) == 0);
    CHECK(close(duplicate) == 0);
    CHECK(close(fd) == 0);
    CHECK(unlink(path) == 0);
    printf("positional-io-test: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
