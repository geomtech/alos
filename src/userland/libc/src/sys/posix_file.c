#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "internal/syscall.h"
#include <sys/syscall.h>

static int checked(long result) {
    if (result < 0) { errno = (int)-result; return -1; }
    return (int)result;
}
int fsync(int fd) { return checked(syscall1(SYS_FSYNC, fd)); }
int pipe2(int output[2], int flags) {
    return checked(syscall2(SYS_PIPE2, (long)output, flags));
}
int pipe(int output[2]) { return pipe2(output, 0); }
int access(const char* path, int mode) {
    return checked(syscall2(SYS_ACCESS, (long)path, mode));
}

/* Le compteur assure seulement la progression des candidats, pas l'entropie.
 * La garantie d'unicite vient du create exclusif atomique dans le noyau. */
static uint64_t sequence;
static int temporary(char* pattern, int directory) {
    if (!pattern) { errno = EINVAL; return -1; }
    size_t length = strlen(pattern);
    if (length < 6 || memcmp(pattern + length - 6, "XXXXXX", 6)) {
        errno = EINVAL; return -1;
    }
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (unsigned attempt = 0; attempt < 4096; attempt++) {
        uint64_t value = __atomic_add_fetch(&sequence, 1, __ATOMIC_RELAXED);
        value += (uint64_t)(unsigned)getpid() * 104729;
        for (size_t i = 0; i < 6; i++) {
            pattern[length - 1 - i] = digits[value % 36];
            value /= 36;
        }
        int result = directory
            ? checked(syscall2(SYS_MKDIR_MODE, (long)pattern, 0700))
            : open(pattern, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (result >= 0) return result;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}
int mkstemp(char* pattern) { return temporary(pattern, 0); }
char* mkdtemp(char* pattern) { return temporary(pattern, 1) < 0 ? NULL : pattern; }
