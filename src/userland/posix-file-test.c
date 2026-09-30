#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int fsync(int);
extern int access(const char*, int);
extern int mkstemp(char*);
extern char* mkdtemp(char*);
static int failures;
#define CHECK(x) do { if (!(x)) { printf("posix-file-test FAIL %d: %s errno=%d\n", __LINE__, #x, errno); failures++; } } while (0)

int main(void) {
    /* Fixture : /posix-test, repertoire Ext2 writable cree par le runner. */
    char file[] = "/posix-test/file-XXXXXXXX";
    char directory[] = "/posix-test/dir-XXXXXXXX";
    int fd = mkstemp(file);
    CHECK(fd >= 0);
    if (fd >= 0) {
        struct stat st;
        CHECK(fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0600);
        CHECK(access(file, 0) == 0);
        CHECK(open(file, O_CREAT | O_EXCL | O_RDWR, 0600) == -1 && errno == EEXIST);
        CHECK(write(fd, "durable", 7) == 7);
        CHECK(fsync(fd) == 0);
        int reopened = open(file, O_WRONLY | O_TRUNC | O_SYNC);
        CHECK(reopened >= 0);
        if (reopened >= 0) {
            CHECK(write(reopened, "durable", 7) == 7);
            CHECK(close(reopened) == 0);
        }
        CHECK(unlink(file) == 0);
        CHECK(access(file, 0) == -1 && errno == ENOENT);
        CHECK(lseek(fd, 0, SEEK_SET) == 0);
        char bytes[8] = {0};
        CHECK(read(fd, bytes, 7) == 7 && !strcmp(bytes, "durable"));
        CHECK(write(fd, "!", 1) == 1);
        CHECK(fsync(fd) == 0);
        CHECK(close(fd) == 0);
    }
    CHECK(mkdtemp(directory) == directory);
    struct stat st;
    CHECK(stat(directory, &st) == 0 && (st.st_mode & 0777) == 0700);
    CHECK(access(directory, 0) == 0);
    CHECK(access(directory, 4) == -1 && errno == ENOTSUP);
    CHECK(rmdir(directory) == 0);
    char invalid[] = "/posix-test/bad-XXXXX";
    CHECK(mkstemp(invalid) == -1 && errno == EINVAL);
    CHECK(fsync(-1) == -1 && errno == EBADF);
    CHECK(access("/posix-test/not-created", 0) == -1 && errno == ENOENT);
    CHECK(access("/posix-test", 8) == -1 && errno == EINVAL);
    printf("posix-file-test: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
