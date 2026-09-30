#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int parse_mode(const char *mode, int *flags, unsigned char *access) {
    if (!mode || !*mode) { errno = EINVAL; return -1; }
    switch (*mode++) {
    case 'r': *flags = O_RDONLY; *access = 1; break;
    case 'w': *flags = O_WRONLY | O_CREAT | O_TRUNC; *access = 2; break;
    case 'a': *flags = O_WRONLY | O_CREAT | O_APPEND; *access = 2; break;
    default: errno = EINVAL; return -1;
    }
    int binary = 0, update = 0;
    for (; *mode; ++mode) {
        if (*mode == 'b' && !binary) binary = 1;
        else if (*mode == '+' && !update) {
            update = 1;
            *flags = (*flags & ~O_ACCMODE) | O_RDWR;
            *access = 3;
        } else { errno = EINVAL; return -1; }
    }
    return 0;
}

FILE *fdopen(int fd, const char *mode) {
    int requested;
    unsigned char access;
    if (parse_mode(mode, &requested, &access)) return NULL;
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return NULL;
    if (((access & 1) && (flags & O_ACCMODE) == O_WRONLY) ||
        ((access & 2) && (flags & O_ACCMODE) == O_RDONLY)) {
        errno = EINVAL;
        return NULL;
    }
    FILE *stream = malloc(sizeof(*stream));
    if (!stream) { errno = ENOMEM; return NULL; }
    if ((requested & O_APPEND) && !(flags & O_APPEND) &&
        fcntl(fd, F_SETFL, flags | O_APPEND)) {
        free(stream);
        return NULL;
    }
    *stream = (FILE){.fd = fd, .access_mode = access};
    return stream;
}

FILE *fopen(const char *pathname, const char *mode) {
    int flags;
    unsigned char access;
    if (!pathname) { errno = EINVAL; return NULL; }
    if (parse_mode(mode, &flags, &access)) return NULL;
    int fd = open(pathname, flags, 0666);
    if (fd < 0) return NULL;
    FILE *stream = fdopen(fd, mode);
    if (!stream) { int error = errno; close(fd); errno = error; }
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) { errno = EINVAL; return EOF; }
    int result = close(stream->fd);
    stream->fd = -1;
    if (stream != stdin && stream != stdout && stream != stderr) free(stream);
    if (result) return EOF;
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
    if (stream->access_mode && !(stream->access_mode & 1)) {
        stream->error = errno = EBADF;
        return 0;
    }
    if (stream->has_wide_pushback) {
        stream->error = errno = EINVAL;
        return 0;
    }
    if (stream->has_pushback) {
        *(unsigned char *)ptr = stream->pushed;
        stream->has_pushback = 0;
        received = 1;
    }
    while (received < bytes) {
        ssize_t result = read(stream->fd, (char *)ptr + received, bytes - received);
        if (result < 0) { stream->error = errno; break; }
        if (!result) { stream->eof = 1; break; }
        received += (size_t)result;
    }
    return received / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) { errno = EINVAL; return -1; }
    if (whence == SEEK_CUR) {
        if (stream->has_wide_pushback) { errno = ENOTSUP; return -1; }
        if (stream->has_pushback) {
            if (offset == (-__LONG_MAX__ - 1L)) { errno = EOVERFLOW; return -1; }
            --offset;
        }
    }
    if (lseek(stream->fd, offset, whence) < 0) return -1;
    stream->has_pushback = stream->has_wide_pushback = 0;
    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) { errno = EINVAL; return -1; }
    if (stream->has_wide_pushback) { errno = ENOTSUP; return -1; }
    off_t offset = lseek(stream->fd, 0, SEEK_CUR);
    if (offset < 0) return -1;
    if (stream->has_pushback) {
        if (!offset) { errno = EINVAL; return -1; }
        --offset;
    }
    return offset;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!size || !nmemb) return 0;
    if (!stream || !ptr || nmemb > (size_t)-1 / size) {
        errno = EINVAL;
        if (stream) stream->error = EINVAL;
        return 0;
    }
    size_t bytes = size * nmemb, sent = 0;
    if (stream->access_mode && !(stream->access_mode & 2)) {
        stream->error = errno = EBADF;
        return 0;
    }
    while (sent < bytes) {
        ssize_t result = write(stream->fd, (const char *)ptr + sent, bytes - sent);
        if (result < 0) { stream->error = errno; break; }
        if (!result) { stream->error = errno = EIO; break; }
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
int fgetc(FILE *stream) {
    unsigned char value;
    return fread(&value, 1, 1, stream) == 1 ? value : EOF;
}
int getc(FILE *stream) { return fgetc(stream); }
char *fgets(char *buffer, int size, FILE *stream) {
    if (!buffer || !stream || size <= 0) { errno = EINVAL; return NULL; }
    int count = 0;
    while (count < size - 1) {
        int value = fgetc(stream);
        if (value == EOF) {
            if (ferror(stream) || !count) return NULL;
            break;
        }
        buffer[count++] = (char)value;
        if (value == '\n') break;
    }
    buffer[count] = '\0';
    return buffer;
}
int ungetc(int value, FILE *stream) {
    if (!stream || stream->fd < 0) { errno = EBADF; return EOF; }
    if (value == EOF || stream->has_pushback) return EOF;
    if (stream->has_wide_pushback) { errno = EINVAL; return EOF; }
    stream->pushed = (unsigned char)value;
    stream->has_pushback = 1;
    stream->eof = 0;
    return stream->pushed;
}
int fputc(int value, FILE *stream) {
    unsigned char byte = (unsigned char)value;
    return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}
int putc(int value, FILE *stream) { return fputc(value, stream); }
