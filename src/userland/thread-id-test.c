#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define WORKERS 4
#define CHECK(x) do { if (!(x)) { printf("thread-id-test: FAIL line=%d\n", __LINE__); return 1; } } while (0)

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned ready, go;
static pthread_t main_thread;

struct observation {
  pid_t tid;
  pthread_t self;
  int failed;
};

static int check_current(pid_t tid) {
  long native = syscall0(SYS_GETTID);
  if (tid <= 0 || native <= 0 || native > INT_MAX ||
      (long)tid != native || (uint32_t)tid != (unsigned long)native)
    return 1;
  errno = EDOM;
  if (gettid() != tid || errno != EDOM) return 1;
  return 0;
}

static void *worker(void *argument) {
  struct observation *observed = argument;
  observed->tid = gettid();
  observed->self = pthread_self();
  observed->failed = check_current(observed->tid) ||
      !pthread_equal(observed->self, pthread_self()) ||
      pthread_equal(observed->self, main_thread);
  int error = pthread_mutex_lock(&mutex);
  if (error) return NULL;
  ready++;
  pthread_cond_broadcast(&changed);
  while (!go) {
    error = pthread_cond_wait(&changed, &mutex);
    if (error) break;
  }
  if (pthread_mutex_unlock(&mutex) || error) return NULL;
  struct timespec delay = {0, 10000000};
  if (nanosleep(&delay, NULL) || check_current(observed->tid) ||
      !pthread_equal(observed->self, pthread_self()))
    observed->failed = 1;
  return argument;
}

int main(void) {
  pid_t main_tid = gettid();
  main_thread = pthread_self();
  CHECK(!check_current(main_tid));
  CHECK(pthread_equal(main_thread, pthread_self()));
  struct observation observed[WORKERS] = {{0}};
  pthread_t threads[WORKERS];
  for (unsigned i = 0; i < WORKERS; i++)
    CHECK(!pthread_create(&threads[i], NULL, worker, &observed[i]));
  CHECK(!pthread_mutex_lock(&mutex));
  while (ready != WORKERS) CHECK(!pthread_cond_wait(&changed, &mutex));
  for (unsigned i = 0; i < WORKERS; i++) {
    CHECK(!observed[i].failed && observed[i].tid != main_tid);
    CHECK(pthread_equal(observed[i].self, threads[i]));
    for (unsigned j = 0; j < i; j++) {
      CHECK(observed[i].tid != observed[j].tid);
      CHECK(!pthread_equal(observed[i].self, observed[j].self));
    }
  }
  go = 1;
  CHECK(!pthread_cond_broadcast(&changed));
  CHECK(!pthread_mutex_unlock(&mutex));
  for (unsigned i = 0; i < WORKERS; i++) {
    void *result = NULL;
    CHECK(!pthread_join(threads[i], &result));
    CHECK(result == &observed[i] && !observed[i].failed);
  }
  CHECK(!check_current(main_tid));
  CHECK(pthread_equal(main_thread, pthread_self()));
  CHECK(!pthread_cond_destroy(&changed));
  CHECK(!pthread_mutex_destroy(&mutex));
  puts("thread-id-test: PASS (native IDs, concurrent uniqueness, stability, opaque pthread handles)");
  return 0;
}
