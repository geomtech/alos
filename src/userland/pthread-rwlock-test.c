#include <pthread.h>
#include <sys/futex.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;
static uint32_t ready, release_readers, active_readers, active_writers;
static uint32_t value, mirror = UINT32_MAX;

static void *overlap_reader(void *argument) {
  (void)argument;
  if (pthread_rwlock_rdlock(&lock)) return (void *)1;
  __atomic_add_fetch(&ready, 1, __ATOMIC_RELEASE);
  futex_wake(&ready, 4);
  while (!__atomic_load_n(&release_readers, __ATOMIC_ACQUIRE)) {
    if (futex_wait(&release_readers, 0, 5000) && errno != EAGAIN)
      return (void *)1;
  }
  return (void *)(intptr_t)pthread_rwlock_unlock(&lock);
}

static void *stress(void *argument) {
  int writer = (intptr_t)argument;
  for (unsigned i = 0; i < 3000; ++i) {
    int error = writer ? pthread_rwlock_wrlock(&lock) : pthread_rwlock_rdlock(&lock);
    if (error) return (void *)1;
    int failed = 0;
    if (writer) {
      if (__atomic_add_fetch(&active_writers, 1, __ATOMIC_SEQ_CST) != 1 ||
          __atomic_load_n(&active_readers, __ATOMIC_SEQ_CST)) failed = 1;
      uint32_t next = __atomic_load_n(&value, __ATOMIC_RELAXED) + 1;
      __atomic_store_n(&value, next, __ATOMIC_RELAXED);
      __atomic_store_n(&mirror, ~next, __ATOMIC_RELAXED);
      __atomic_sub_fetch(&active_writers, 1, __ATOMIC_SEQ_CST);
    } else {
      __atomic_add_fetch(&active_readers, 1, __ATOMIC_SEQ_CST);
      if (__atomic_load_n(&active_writers, __ATOMIC_SEQ_CST) ||
          __atomic_load_n(&value, __ATOMIC_RELAXED) !=
              ~__atomic_load_n(&mirror, __ATOMIC_RELAXED)) failed = 1;
      __atomic_sub_fetch(&active_readers, 1, __ATOMIC_SEQ_CST);
    }
    if (pthread_rwlock_unlock(&lock) || failed) return (void *)1;
  }
  return NULL;
}

int main(void) {
  pthread_t threads[6];
  errno = ERANGE;
  if (pthread_mutex_lock(&lock.gate) ||
      pthread_rwlock_tryrdlock(&lock) != EBUSY ||
      pthread_rwlock_trywrlock(&lock) != EBUSY ||
      pthread_mutex_unlock(&lock.gate)) goto fail;
  if (pthread_rwlock_tryrdlock(&lock) || errno != ERANGE ||
      pthread_rwlock_tryrdlock(&lock) || pthread_rwlock_trywrlock(&lock) != EBUSY ||
      pthread_rwlock_destroy(&lock) != EBUSY ||
      pthread_rwlock_unlock(&lock) || pthread_rwlock_unlock(&lock) ||
      pthread_rwlock_trywrlock(&lock) || pthread_rwlock_tryrdlock(&lock) != EBUSY ||
      pthread_rwlock_trywrlock(&lock) != EBUSY ||
      pthread_rwlock_wrlock(&lock) != EDEADLK ||
      pthread_rwlock_unlock(&lock)) goto fail;
  for (int i = 0; i < 4; ++i)
    if (pthread_create(&threads[i], NULL, overlap_reader, NULL)) goto fail;
  for (;;) {
    uint32_t current = __atomic_load_n(&ready, __ATOMIC_ACQUIRE);
    if (current == 4) break;
    if (futex_wait(&ready, current, 5000) && errno != EAGAIN) goto fail;
  }
  if (pthread_rwlock_trywrlock(&lock) != EBUSY ||
      pthread_rwlock_destroy(&lock) != EBUSY) goto fail;
  __atomic_store_n(&release_readers, 1, __ATOMIC_RELEASE);
  if (futex_wake(&release_readers, 4) < 0) goto fail;
  for (int i = 0; i < 4; ++i) {
    void *result;
    if (pthread_join(threads[i], &result) || result) goto fail;
  }
  for (int i = 0; i < 6; ++i)
    if (pthread_create(&threads[i], NULL, stress, (void *)(intptr_t)(i >= 4)))
      goto fail;
  for (int i = 0; i < 6; ++i) {
    void *result;
    if (pthread_join(threads[i], &result) || result) goto fail;
  }
  if (value != 6000 || mirror != ~value || pthread_rwlock_destroy(&lock) ||
      pthread_rwlock_rdlock(&lock) != EINVAL ||
      pthread_rwlock_unlock(&lock) != EINVAL ||
      pthread_rwlock_destroy(&lock) != EINVAL ||
      pthread_rwlock_init(&lock, NULL) ||
      pthread_rwlock_wrlock(&lock) || pthread_rwlock_unlock(&lock) ||
      pthread_rwlock_destroy(&lock)) goto fail;
  puts("pthread-rwlock-test: PASS");
  return 0;
fail:
  puts("pthread-rwlock-test: FAIL");
  return 1;
}
