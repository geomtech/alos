/* src/userland/libc/src/stdio/stdio.c */
#include "printf_sink.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>

static FILE standard_input = {STDIN_FILENO, 0, 0};
static FILE standard_output = {STDOUT_FILENO, 0, 0};
static FILE standard_error = {STDERR_FILENO, 0, 0};
FILE *stdin = &standard_input;
FILE *stdout = &standard_output;
FILE *stderr = &standard_error;

void __alos_sink_write(struct __alos_sink *sink, const char *data, size_t length) {
  if (sink->stream) {
    if (fwrite(data, 1, length, sink->stream) != length) sink->error = 1;
  } else {
    size_t remaining = sink->capacity - sink->used;
    size_t copied = length < remaining ? length : remaining;
    if (copied) memcpy(sink->buffer + sink->used, data, copied);
    sink->used += copied;
  }
}

int vfprintf(FILE *stream, const char *format, va_list arguments) {
  if (!stream || !format) { errno = EINVAL; return EOF; }
  struct __alos_sink sink = {stream, NULL, 0, 0, stream->error != 0};
  return __alos_vformat(&sink, format, arguments);
}

int fprintf(FILE *stream, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vfprintf(stream, format, arguments);
  va_end(arguments);
  return result;
}

int vprintf(const char *format, va_list arguments) {
  return vfprintf(stdout, format, arguments);
}

int printf(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vprintf(format, arguments);
  va_end(arguments);
  return result;
}

int vsnprintf(char *str, size_t size, const char *format, va_list arguments) {
  if (!format || (size && !str)) { errno = EINVAL; return -1; }
  struct __alos_sink sink = {NULL, str, size ? size - 1 : 0, 0, 0};
  int result = __alos_vformat(&sink, format, arguments);
  if (size) str[sink.used] = '\0';
  return result;
}

int vsprintf(char *str, const char *format, va_list arguments) {
  return vsnprintf(str, (size_t)-1, format, arguments);
}

int sprintf(char *str, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsprintf(str, format, arguments);
  va_end(arguments);
  return result;
}

int snprintf(char *str, size_t size, const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(str, size, format, arguments);
  va_end(arguments);
  return result;
}

int remove(const char *path) {
  int result = unlink(path);
  if (result) { errno = EIO; return -1; }
  return 0;
}

int getchar(void) {
  unsigned char c;
  return fread(&c, 1, 1, stdin) == 1 ? c : EOF;
}

int putchar(int c) {
  unsigned char ch = (unsigned char)c;
  return fwrite(&ch, 1, 1, stdout) == 1 ? ch : EOF;
}

int puts(const char *s) {
  size_t length = strlen(s);
  if (fwrite(s, 1, length, stdout) != length ||
      fwrite("\n", 1, 1, stdout) != 1) return EOF;
  return 1;
}
