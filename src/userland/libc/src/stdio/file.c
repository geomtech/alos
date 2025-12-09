#include <stdio.h>

FILE *fopen(const char *pathname, const char *mode) {
    (void)pathname;
    (void)mode;
    return NULL;
}

int fclose(FILE *stream) {
    (void)stream;
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr;
    (void)size;
    (void)nmemb;
    (void)stream;
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    return -1;
}

long ftell(FILE *stream) {
    (void)stream;
    return -1L;
}
