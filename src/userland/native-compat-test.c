#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("native-compat-test FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)

static uint32_t bits(float value) {
  union { float value; uint32_t bits; } x = {.value = value};
  return x.bits;
}

static void *loader_thread(void *unused) {
  (void)unused;
  CHECK(dlerror() == NULL);
  CHECK(dlsym(RTLD_DEFAULT, "malloc") == NULL && errno == ENOTSUP);
  CHECK(dlerror() != NULL);
  CHECK(dlerror() == NULL);
  return NULL;
}

int main(void) {
  CHECK(MAXPATHLEN == PATH_MAX && O_NDELAY == O_NONBLOCK && O_NOCTTY == 0);
  CHECK(bits(ldexpf(-0.0f, INT_MAX)) == 0x80000000);
  CHECK(ldexpf(1.5f, 5) == 48.0f);
  CHECK(bits(ldexpf(1.0f, -149)) == 1);
  CHECK(bits(ldexpf(1.0f, -150)) == 0);
  CHECK(bits(ldexpf(3.0f, -150)) == 2);
  CHECK(bits(ldexpf(0x1p-149f, 149)) == bits(1.0f));
  CHECK(ldexpf(0x1.fffffep127f, -127) == 0x1.fffffep0f);
  errno = 0;
  CHECK(isinf(ldexpf(1.0f, INT_MAX)) && errno == ERANGE);
  errno = 0;
  CHECK(ldexpf(1.0f, INT_MIN) == 0.0f && errno == ERANGE);
  errno = 0;
  CHECK(isnan(ldexpf(NAN, 8)) && errno == 0);
  CHECK(isinf(ldexpf(INFINITY, INT_MIN)));

  struct alos_malloc_stats before, during, after;
  CHECK(alos_malloc_get_stats(&before) == 0);
  void *allocation = malloc(257);
  CHECK(allocation != NULL);
  if (allocation) {
    memset(allocation, 0x5a, 257);
    CHECK(malloc_usable_size(allocation) >= 257);
    CHECK(alos_malloc_get_stats(&during) == 0);
    CHECK(during.allocated_blocks == before.allocated_blocks + 1);
    CHECK(during.allocated_bytes >= before.allocated_bytes + 257);
    CHECK(during.arena_bytes == during.allocated_bytes + during.free_bytes + during.metadata_bytes);
    free(allocation);
    CHECK(alos_malloc_get_stats(&after) == 0);
    CHECK(after.allocated_blocks == before.allocated_blocks);
    CHECK(after.allocated_bytes == before.allocated_bytes);
  }
  CHECK(malloc_usable_size(NULL) == 0);
  errno = 0;
  CHECK(alos_malloc_get_stats(NULL) == -1 && errno == EINVAL);

  CHECK(dlerror() == NULL);
  CHECK(dlopen(NULL, RTLD_NOW) == NULL && errno == ENOTSUP);
  pthread_t thread;
  int created = pthread_create(&thread, NULL, loader_thread, NULL);
  CHECK(created == 0);
  if (!created) CHECK(pthread_join(thread, NULL) == 0);
  CHECK(dlerror() != NULL);
  CHECK(dlerror() == NULL);
  CHECK(dlclose(NULL) == -1 && errno == ENOTSUP);
  CHECK(dlerror() != NULL);
  Dl_info info = {.dli_fname = "unchanged"};
  CHECK(dladdr((void *)main, &info) == 0 && errno == ENOTSUP);
  CHECK(!strcmp(info.dli_fname, "unchanged"));
  CHECK(dlerror() != NULL && dlerror() == NULL);
  printf("native-compat-test %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
