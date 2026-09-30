#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

FILE *fopen(const char *pathname, const char *mode) {
    if (!pathname || !mode) { errno = EINVAL; return NULL; }
    if (mode[0] != 'r' || (mode[1] && (mode[1] != 'b' || mode[2]))) {
        errno = ENOTSUP;
        return NULL;
    }
    int fd = open(pathname, O_RDONLY);
    if (fd < 0) { errno = EIO; return NULL; }
    FILE *stream = malloc(sizeof(*stream));
    if (!stream) { close(fd); return NULL; }
    *stream = (FILE){fd, 0, 0};
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) { errno = EINVAL; return EOF; }
    int result = close(stream->fd);
    stream->fd = -1;
    if (stream != stdin && stream != stdout && stream != stderr) free(stream);
    if (result) { errno = EIO; return EOF; }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!size || !nmemb) return 0;
    if (!stream || !ptr || nmemb > (size_t)-1 / size) {
        errno = EINVAL;
        if (stream) stream->error = EINVAL;
        return 0;
    }
    size_t bytes = size * nmemb, received = 0;
    while (received < bytes) {
        ssize_t result = read(stream->fd, (char *)ptr + received, bytes - received);
        if (result < 0) { stream->error = errno = EIO; break; }
        if (!result) { stream->eof = 1; break; }
        received += (size_t)result;
    }
    return received / size;
}

int fseek(FILE *stream, long offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    errno = ENOTSUP;
    return -1;
}

long ftell(FILE *stream) {
    (void)stream;
    errno = ENOTSUP;
    return -1L;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!size || !nmemb) return 0;
    if (!stream || !ptr || nmemb > (size_t)-1 / size) {
        errno = EINVAL;
        if (stream) stream->error = EINVAL;
        return 0;
    }
    size_t bytes = size * nmemb, sent = 0;
    while (sent < bytes) {
        ssize_t result = write(stream->fd, (const char *)ptr + sent, bytes - sent);
        if (result <= 0) { stream->error = errno = EIO; break; }
        sent += (size_t)result;
    }
    return sent / size;
}

int fflush(FILE *stream) {
    /* Flux non bufferises : les ecritures sont deja transmises au syscall. */
    if (stream && stream->fd < 0) { errno = EBADF; return EOF; }
    return 0;
}
int feof(FILE *stream) { return stream->eof; }
int ferror(FILE *stream) { return stream->error; }
void clearerr(FILE *stream) { stream->error = stream->eof = 0; }
int fileno(FILE *stream) {
    if (!stream || stream->fd < 0) { errno = EBADF; return -1; }
    return stream->fd;
}
