#include <errno.h>
#include <sys/time.h>
#include <utime.h>

int utime(const char *path, const struct utimbuf *times) {
  if (!times) return utimes(path, 0);
  struct timeval tv[2] = {{times->actime, 0}, {times->modtime, 0}};
  return utimes(path, tv);
}
