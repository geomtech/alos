#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/futex.h>
#include <sys/syscall.h>
#include <sys/vminfo.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { printf("pthread-attr-test: FAIL line=%d\n", __LINE__); return 1; } } while (0)

static uint32_t completed, release_worker;
static int worker_error;
static size_t requested;
static pthread_t main_thread;
static uint32_t main_tid;

static int contains(void *base, size_t size, const void *address) {
  uintptr_t start = (uintptr_t)base, value = (uintptr_t)address;
  return value >= start && value - start < size;
}

static int inspect_stack(pthread_t thread, const void *local, size_t expected,
                         int detached) {
  pthread_attr_t attr;
  void *base = NULL;
  size_t size = 0;
  int state = -1;
  errno = EDOM;
  if (pthread_getattr_np(thread, &attr) || errno != EDOM ||
      pthread_attr_getstack(&attr, &base, &size) ||
      pthread_attr_getdetachstate(&attr, &state) ||
      !contains(base, size, local) || (expected && size != expected) ||
      state != detached || pthread_attr_destroy(&attr) || errno != EDOM)
    return 1;
  if (expected) {
    uint64_t length;
    if (syscall3(SYS_THREAD_STACK, main_tid, (long)((char *)base - 4096),
                 (long)&length) != -EFAULT ||
        syscall3(SYS_THREAD_STACK, main_tid, (long)((char *)base + size),
                 (long)&length) != -EFAULT)
      return 1;
  }
  return 0;
}

static void *worker(void *argument) {
  int local;
  int detached = (int)(uintptr_t)argument;
  worker_error = inspect_stack(pthread_self(), &local, requested, detached);
  pthread_attr_t main_attr;
  if (pthread_getattr_np(main_thread, &main_attr) ||
      pthread_attr_destroy(&main_attr))
    worker_error = 1;
  __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
  futex_wake(&completed, 1);
  while (!__atomic_load_n(&release_worker, __ATOMIC_ACQUIRE)) {
    if (futex_wait(&release_worker, 0, 5000) && errno != EAGAIN)
      return (void *)1;
  }
  return (void *)(uintptr_t)worker_error;
}

static int wait_completed(void) {
  while (!__atomic_load_n(&completed, __ATOMIC_ACQUIRE)) {
    if (futex_wait(&completed, 0, 5000) && errno != EAGAIN) return 1;
  }
  return 0;
}

int main(void) {
  int local;
  main_thread = pthread_self();
  main_tid = (uint32_t)syscall0(SYS_GETTID);
  CHECK(!inspect_stack(main_thread, &local, 0, PTHREAD_CREATE_JOINABLE));
  pthread_attr_t attr;
  errno = EDOM;
  CHECK(pthread_attr_init(NULL) == EINVAL && errno == EDOM);
  CHECK(!pthread_attr_init(&attr));
  CHECK(pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN - 1) == EINVAL);
  CHECK(pthread_attr_setstacksize(&attr, (size_t)-1) == EINVAL);
  CHECK(pthread_attr_setdetachstate(&attr, -1) == EINVAL);
  CHECK(pthread_attr_getstack(&attr, NULL, &requested) == EINVAL);
  CHECK(pthread_getattr_np(NULL, &attr) == EINVAL && errno == EDOM);
  uint64_t query_base, query_size;
  CHECK(syscall3(SYS_THREAD_STACK, UINT32_MAX, (long)&query_base,
                 (long)&query_size) == -ESRCH);
  CHECK(syscall3(SYS_THREAD_STACK, main_tid, 0, (long)&query_size) == -EFAULT);
  CHECK(errno == EDOM);
  CHECK(!pthread_attr_destroy(&attr));
  CHECK(pthread_attr_destroy(&attr) == EINVAL && errno == EDOM);
  CHECK(pthread_attr_setstacksize(&attr, 65536) == EINVAL);
  pthread_t invalid = (pthread_t)(uintptr_t)1;
  CHECK(pthread_create(&invalid, &attr, worker, NULL) == EINVAL &&
        invalid == (pthread_t)(uintptr_t)1 && errno == EDOM);
  CHECK(!pthread_getattr_np(main_thread, &attr));
  CHECK(pthread_create(&invalid, &attr, worker, NULL) == ENOTSUP &&
        invalid == (pthread_t)(uintptr_t)1 && errno == EDOM);
  CHECK(!pthread_attr_destroy(&attr));
  CHECK(!pthread_attr_init(&attr));
  CHECK(!pthread_attr_setstacksize(&attr, 65537));
  CHECK(!pthread_attr_getstacksize(&attr, &requested) && requested == 69632);
  vm_info_t before, after;
  CHECK(!vminfo(&before));
  pthread_t thread;
  errno = EDOM;
  CHECK(!pthread_create(&thread, &attr, worker, NULL) && errno == EDOM);
  CHECK(!wait_completed() && !worker_error);
  pthread_attr_t actual;
  void *base;
  size_t size;
  CHECK(!pthread_getattr_np(thread, &actual));
  CHECK(!pthread_attr_getstack(&actual, &base, &size));
  CHECK(size == requested && !((uintptr_t)base & 4095));
  CHECK(!pthread_attr_destroy(&actual));
  __atomic_store_n(&release_worker, 1, __ATOMIC_RELEASE);
  futex_wake(&release_worker, 1);
  void *result;
  CHECK(!pthread_join(thread, &result) && !result);
  CHECK(!vminfo(&after) && before.virtual_size == after.virtual_size &&
        before.physical_free == after.physical_free);
  CHECK(!pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED));
  for (unsigned i = 0; i < 20; i++) {
    completed = release_worker = 0;
    CHECK(!vminfo(&before));
    errno = EDOM;
    CHECK(!pthread_create(&thread, &attr, worker,
                          (void *)(uintptr_t)PTHREAD_CREATE_DETACHED) &&
          errno == EDOM);
    CHECK(!wait_completed() && !worker_error);
    CHECK(pthread_join(thread, NULL) == EINVAL);
    CHECK(pthread_detach(thread) == EINVAL);
    __atomic_store_n(&release_worker, 1, __ATOMIC_RELEASE);
    futex_wake(&release_worker, 1);
    struct timespec delay = {0, 1000000};
    unsigned attempts;
    for (attempts = 0; attempts < 5000; attempts++) {
      CHECK(!vminfo(&after));
      if (before.virtual_size == after.virtual_size &&
          before.physical_free == after.physical_free) break;
      CHECK(!nanosleep(&delay, NULL));
    }
    CHECK(attempts < 5000);
  }
  CHECK(!pthread_attr_destroy(&attr));
  puts("pthread-attr-test: PASS");
  return 0;
}
