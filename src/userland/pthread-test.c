#include <pthread.h>
#include "runtime-test.h"
#include <sys/vminfo.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <assert.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t go_cv = PTHREAD_COND_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t key;
static unsigned ready, go, counter, once_calls, destructor_calls;
static uint32_t detached_done;
static pthread_t main_thread;
static _Thread_local int local = 23;
#define CHECK(x) do { if (!(x)) { printf("pthread-test: FAIL line=%d\n", __LINE__); return 1; } } while (0)

static void initialize_once(void) { once_calls++; }
static void destroy_value(void *value) {
  if (value) __atomic_add_fetch(&destructor_calls, 1, __ATOMIC_RELAXED);
}
static void *worker(void *argument) {
  if (local != 23 || pthread_equal(pthread_self(), main_thread) ||
      pthread_once(&once, initialize_once) ||
      pthread_setspecific(key, argument) || pthread_getspecific(key) != argument)
    return (void *)1;
  local = (int)(long)argument;
  if (pthread_mutex_lock(&mutex)) return (void *)1;
  ready++;
  pthread_cond_signal(&ready_cv);
  while (!go) if (pthread_cond_wait(&go_cv, &mutex)) return (void *)1;
  pthread_mutex_unlock(&mutex);
  for (unsigned i = 0; i < 20000; i++) {
    if (pthread_mutex_lock(&mutex)) return (void *)1;
    counter++;
    if (local != (int)(long)argument) return (void *)1;
    if (pthread_mutex_unlock(&mutex)) return (void *)1;
    if ((i & 127) == 0) {
      unsigned char *buffer = malloc(73);
      if (!buffer || ((uint64_t)buffer & 15)) return (void *)1;
      for (unsigned j = 0; j < 73; j++) buffer[j] = (unsigned char)local;
      unsigned char *larger = realloc(buffer, 145);
      if (!larger || ((uint64_t)larger & 15)) return (void *)1;
      for (unsigned j = 0; j < 73; j++)
        if (larger[j] != (unsigned char)local) return (void *)1;
      free(larger);
    }
  }
  return argument;
}
static void *short_worker(void *argument) {
  if (local != 23 || pthread_setspecific(key, argument)) return (void *)1;
  return argument;
}
static void *detached_worker(void *argument) {
  (void)argument;
  struct timespec delay = {0, 10000000};
  nanosleep(&delay, NULL);
  __atomic_add_fetch(&detached_done, 1, __ATOMIC_RELEASE);
  futex_wake(&detached_done, 64);
  return NULL;
}
static void *forever_worker(void *argument) {
  (void)argument;
  for (;;) __asm__ volatile("pause");
}
int main(void) {
  main_thread = pthread_self();
  CHECK(pthread_key_create(&key, destroy_value) == 0);
  CHECK(pthread_mutex_trylock(&mutex) == 0);
  CHECK(pthread_mutex_trylock(&mutex) == EBUSY);
  CHECK(pthread_mutex_destroy(&mutex) == EBUSY);
  CHECK(pthread_mutex_unlock(&mutex) == 0);
  pthread_t threads[4];
  for (long i = 0; i < 4; i++) CHECK(!pthread_create(&threads[i], NULL, worker, (void *)(i + 2)));
  CHECK(!pthread_mutex_lock(&mutex));
  while (ready != 4) CHECK(!pthread_cond_wait(&ready_cv, &mutex));
  go = 1;
  CHECK(!pthread_cond_broadcast(&go_cv));
  CHECK(!pthread_mutex_unlock(&mutex));
  for (long i = 0; i < 4; i++) {
    void *result;
    CHECK(!pthread_join(threads[i], &result) && result == (void *)(i + 2));
  }
  CHECK(counter == 80000 && once_calls == 1 && destructor_calls == 4 && local == 23);
  CHECK(!pthread_mutex_lock(&mutex));
  struct timespec deadline;
  CHECK(!clock_gettime(CLOCK_REALTIME, &deadline));
  deadline.tv_nsec += 20000000;
  if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
  CHECK(pthread_cond_timedwait(&go_cv, &mutex, &deadline) == ETIMEDOUT);
  struct timespec after_wait;
  CHECK(!clock_gettime(CLOCK_REALTIME, &after_wait));
  CHECK(after_wait.tv_sec > deadline.tv_sec ||
        (after_wait.tv_sec == deadline.tv_sec &&
         after_wait.tv_nsec >= deadline.tv_nsec));
  CHECK(!pthread_mutex_unlock(&mutex));
  vm_info_t join_before, join_after;
  CHECK(!vminfo(&join_before));
  for (long i = 1; i <= 1000; i++) {
    pthread_t thread;
    void *result;
    CHECK(!pthread_create(&thread, NULL, short_worker, (void *)i));
    CHECK(!pthread_join(thread, &result) && result == (void *)i);
  }
  CHECK(!vminfo(&join_after) &&
        join_after.physical_free == join_before.physical_free &&
        join_after.virtual_size == join_before.virtual_size);
  CHECK(destructor_calls == 1004);
  vm_info_t before, after;
  CHECK(!vminfo(&before));
  for (int i = 0; i < 20; i++) {
    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, detached_worker, NULL));
    CHECK(!pthread_detach(thread));
  }
  for (;;) {
    uint32_t done = __atomic_load_n(&detached_done, __ATOMIC_ACQUIRE);
    if (done == 20) break;
    CHECK(!futex_wait(&detached_done, done, 5000) || errno == EAGAIN);
  }
  struct timespec grace = {0, 20000000};
  CHECK(!nanosleep(&grace, NULL));
  CHECK(!vminfo(&after));
  printf("pthread-test: detached physical-delta=%d virtual-delta=%d resident-delta=%d\n",
         (int)(after.physical_free - before.physical_free),
         (int)(after.virtual_size - before.virtual_size),
         (int)(after.resident_size - before.resident_size));
  CHECK(after.physical_free == before.physical_free);
  CHECK(!pthread_key_delete(key));
  CHECK(!pthread_cond_destroy(&ready_cv) && !pthread_cond_destroy(&go_cv));
  CHECK(!pthread_mutex_destroy(&mutex));
  pthread_mutexattr_t attr;
  pthread_mutex_t recursive;
  CHECK(!pthread_mutexattr_init(&attr));
  CHECK(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
  CHECK(!pthread_mutex_init(&recursive, &attr));
  CHECK(!pthread_mutexattr_destroy(&attr));
  CHECK(!pthread_mutex_lock(&recursive) && !pthread_mutex_lock(&recursive));
  CHECK(!pthread_mutex_unlock(&recursive) && !pthread_mutex_unlock(&recursive));
  CHECK(!pthread_mutex_destroy(&recursive));
  for (size_t alignment = 16; alignment <= 4096; alignment *= 2) {
    unsigned char *buffer = aligned_alloc(alignment, 2 * alignment);
    CHECK(buffer && !((uint64_t)buffer & (alignment - 1)));
    for (size_t i = 0; i < 2 * alignment; i++) buffer[i] = 0xA5;
    free(buffer);
    void *unaligned_size = NULL;
    CHECK(!posix_memalign(&unaligned_size, alignment, 73) && unaligned_size &&
          !((uint64_t)unaligned_size & (alignment - 1)));
    free(unaligned_size);
  }
  CHECK(!aligned_alloc(32, 33) && errno == EINVAL);
  void *unchanged = (void *)0x1234;
  CHECK(posix_memalign(&unchanged, 3, 64) == EINVAL && unchanged == (void *)0x1234);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, forever_worker, NULL)) _exit(1);
    struct timespec delay = {0, 20000000};
    nanosleep(&delay, NULL);
    assert(0 && "pthread abort group");
  }
  int status;
  CHECK(waitpid(child, &status, 0) == child && status == 134);
  puts("pthread-test: PASS (1000 joins, detach cleanup, contention, conditions, once, keys)");
  return 0;
}
