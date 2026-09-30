#include <stdio.h>
#include <string.h>
#include <errno.h>

int fputs(const char *text, FILE *stream) {
  size_t length = strlen(text);
  return fwrite(text, 1, length, stream) == length ? 0 : EOF;
}

void perror(const char *prefix) {
  int saved_errno = errno;
  if (prefix && *prefix) {
    if (fputs(prefix, stderr) == EOF || fputs(": ", stderr) == EOF) return;
  }
  if (fputs(strerror(saved_errno), stderr) == EOF) return;
  if (fputc('\n', stderr) == EOF) return;
  errno = saved_errno;
}
