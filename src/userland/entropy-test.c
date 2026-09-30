#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>

#define CHECK(x) do { if (!(x)) { \
    printf("entropy-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
    return 1; \
} } while (0)

static void *worker(void *unused) {
    (void)unused;
    unsigned char bytes[256];
    for (unsigned i = 0; i < 64; i++)
        if (getentropy(bytes, sizeof(bytes))) return (void *)1;
    return NULL;
}

int main(int argc, char **argv) {
    int unavailable = argc > 1 && !strcmp(argv[1], "--unavailable");
    unsigned char bytes[257];
    memset(bytes, 0xa5, sizeof(bytes));
    errno = 123;
    CHECK(getentropy(NULL, 0) == 0 && errno == 123);
    CHECK(getentropy((void *)(uintptr_t)-1, 0) == 0);
    CHECK(getentropy(bytes, 257) == -1 && errno == EIO);
    CHECK(getentropy(NULL, 257) == -1 && errno == EIO);
    CHECK(getentropy(bytes, (size_t)-1) == -1 && errno == EIO);
    CHECK(getentropy(NULL, 1) == -1 && errno == EFAULT);
    CHECK(getentropy((void *)(uintptr_t)-1, 1) == -1 && errno == EFAULT);
    unsigned char *pages = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(pages != MAP_FAILED);
    CHECK(mprotect(pages + 4096, 4096, PROT_NONE) == 0);
    CHECK(getentropy(pages + 4096, 0) == 0);
    CHECK(getentropy(pages + 4096, 1) == -1 && errno == EFAULT);
    CHECK(getentropy(pages + 4096 - 16, 32) == -1 && errno == EFAULT);
    CHECK(mprotect(pages, 4096, PROT_READ) == 0);
    CHECK(getentropy(pages, 1) == -1 && errno == EFAULT);
    CHECK(mprotect(pages, 4096, PROT_READ | PROT_WRITE) == 0);
    if (unavailable) {
        CHECK(getentropy(bytes, 256) == -1 && errno == ENOSYS);
        for (unsigned i = 0; i < sizeof(bytes); i++) CHECK(bytes[i] == 0xa5);
        CHECK(getentropy(pages, 1) == -1 && errno == ENOSYS);
        CHECK(munmap(pages, 8192) == 0);
        puts("entropy-test: unavailable PASS");
        return 0;
    }
    CHECK(getentropy(bytes, 256) == 0);
    CHECK(bytes[256] == 0xa5);
    CHECK(getentropy(pages, 256) == 0);
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; i++)
        CHECK(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    for (unsigned i = 0; i < 4; i++) {
        void *result;
        CHECK(pthread_join(threads[i], &result) == 0 && result == NULL);
    }
    CHECK(munmap(pages, 8192) == 0);
    puts("entropy-test: source/concurrent PASS (not a cryptographic health test)");
    return 0;
}
