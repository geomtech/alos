#include <dlfcn.h>
#include <errno.h>

static __thread int error_pending;
static char unsupported[] = "ALOS: dynamic loading and symbol lookup unsupported";

static void unavailable(void) {
  errno = ENOTSUP;
  error_pending = 1;
}

void *dlopen(const char *path, int flags) {
  (void)path;
  (void)flags;
  unavailable();
  return 0;
}

void *dlsym(void *handle, const char *name) {
  (void)handle;
  (void)name;
  unavailable();
  return 0;
}

int dlclose(void *handle) {
  (void)handle;
  unavailable();
  return -1;
}

char *dlerror(void) {
  if (!error_pending) return 0;
  error_pending = 0;
  return unsupported;
}

int dladdr(const void *address, Dl_info *info) {
  (void)address;
  (void)info;
  unavailable();
  return 0;
}
