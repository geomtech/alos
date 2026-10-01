#include <errno.h>
#include <semaphore.h>
#include <stdint.h>
#include <sys/futex.h>
#include <time.h>

int sem_init(sem_t *semaphore, int pshared, unsigned int value) {
  if (value > SEM_VALUE_MAX) { errno = EINVAL; return -1; }
  /* Les futex ALOS sont prives a l'espace d'adressage. */
  if (pshared) { errno = ENOSYS; return -1; }
  semaphore->value = value;
  semaphore->waiters = 0;
  return 0;
}

int sem_destroy(sem_t *semaphore) {
  if (__atomic_load_n(&semaphore->waiters, __ATOMIC_SEQ_CST)) {
    errno = EBUSY;
    return -1;
  }
  return 0;
}

int sem_post(sem_t *semaphore) {
  uint32_t value = __atomic_load_n(&semaphore->value, __ATOMIC_RELAXED);
  do {
    if (value >= SEM_VALUE_MAX) { errno = EOVERFLOW; return -1; }
  } while (!__atomic_compare_exchange_n(&semaphore->value, &value, value + 1, 1,
                                        __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
  /* SEQ_CST des deux cotes : un waiter enregistre apres cette lecture verra
   * la nouvelle valeur au moment du futex_wait (EAGAIN). */
  if (__atomic_load_n(&semaphore->waiters, __ATOMIC_SEQ_CST) &&
      futex_wake(&semaphore->value, 1) < 0)
    return -1;
  return 0;
}

int sem_trywait(sem_t *semaphore) {
  uint32_t value = __atomic_load_n(&semaphore->value, __ATOMIC_RELAXED);
  while (value) {
    if (__atomic_compare_exchange_n(&semaphore->value, &value, value - 1, 1,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
      return 0;
  }
  errno = EAGAIN;
  return -1;
}

/* Delai restant en millisecondes (arrondi superieur, >= 1) ou ETIMEDOUT. */
static int remaining_ms(const struct timespec *deadline, uint32_t *timeout) {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now)) return errno;
  if (deadline->tv_sec < now.tv_sec ||
      (deadline->tv_sec == now.tv_sec && deadline->tv_nsec <= now.tv_nsec))
    return ETIMEDOUT;
  uint64_t seconds = (uint64_t)(deadline->tv_sec - now.tv_sec);
  if (seconds > UINT32_MAX / 1000 - 1) seconds = UINT32_MAX / 1000 - 1;
  int64_t nanoseconds = (int64_t)seconds * 1000000000 + deadline->tv_nsec - now.tv_nsec;
  uint64_t ms = ((uint64_t)nanoseconds + 999999) / 1000000;
  *timeout = ms ? (uint32_t)ms : 1;
  return 0;
}

static int semaphore_wait(sem_t *semaphore, const struct timespec *deadline) {
  for (;;) {
    if (!sem_trywait(semaphore)) return 0;
    uint32_t timeout = 0;
    if (deadline) {
      int error = remaining_ms(deadline, &timeout);
      if (error) { errno = error; return -1; }
    }
    __atomic_add_fetch(&semaphore->waiters, 1, __ATOMIC_SEQ_CST);
    int result = futex_wait(&semaphore->value, 0, timeout);
    int error = result ? errno : 0;
    __atomic_sub_fetch(&semaphore->waiters, 1, __ATOMIC_SEQ_CST);
    if (result && error != EAGAIN && error != ETIMEDOUT) {
      errno = error;
      return -1;
    }
  }
}

int sem_wait(sem_t *semaphore) { return semaphore_wait(semaphore, NULL); }

int sem_timedwait(sem_t *semaphore, const struct timespec *deadline) {
  if (!sem_trywait(semaphore)) return 0;
  if (!deadline || deadline->tv_sec < 0 || deadline->tv_nsec < 0 ||
      deadline->tv_nsec >= 1000000000) {
    errno = EINVAL;
    return -1;
  }
  return semaphore_wait(semaphore, deadline);
}

int sem_getvalue(sem_t *semaphore, int *value) {
  *value = (int)__atomic_load_n(&semaphore->value, __ATOMIC_SEQ_CST);
  return 0;
}
