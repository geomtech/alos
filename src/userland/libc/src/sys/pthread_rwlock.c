#include <pthread.h>
#include <errno.h>
#include <sys/syscall.h>

int pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr) {
  if (!lock) return EINVAL;
  if (attr) return ENOTSUP;
  *lock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
  return 0;
}

static int finish(pthread_rwlock_t *lock, int result, int saved_errno) {
  int error = pthread_mutex_unlock(&lock->gate);
  errno = saved_errno;
  return result ? result : error;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
  if (!lock) return EINVAL;
  int saved_errno = errno;
  int error = pthread_mutex_lock(&lock->gate);
  if (error) { errno = saved_errno; return error; }
  if (lock->destroyed) return finish(lock, EINVAL, saved_errno);
  if (lock->readers || lock->writer || lock->waiters)
    return finish(lock, EBUSY, saved_errno);
  lock->destroyed = 1;
  return finish(lock, 0, saved_errno);
}

static int acquire(pthread_rwlock_t *lock, int write, int try) {
  if (!lock) return EINVAL;
  int saved_errno = errno;
  int error = try ? pthread_mutex_trylock(&lock->gate) :
                    pthread_mutex_lock(&lock->gate);
  if (error) { errno = saved_errno; return error; }
  uint32_t tid = (uint32_t)syscall0(SYS_GETTID);
  for (;;) {
    if (lock->destroyed) return finish(lock, EINVAL, saved_errno);
    if (!lock->writer && (!write || !lock->readers)) break;
    if (try) return finish(lock, EBUSY, saved_errno);
    if (lock->writer == tid) return finish(lock, EDEADLK, saved_errno);
    if (lock->waiters == UINT32_MAX) return finish(lock, EAGAIN, saved_errno);
    ++lock->waiters;
    error = pthread_cond_wait(&lock->changed, &lock->gate);
    --lock->waiters;
    if (error) return finish(lock, error, saved_errno);
  }
  if (write) lock->writer = tid;
  else {
    if (lock->readers == UINT32_MAX) return finish(lock, EAGAIN, saved_errno);
    ++lock->readers;
  }
  return finish(lock, 0, saved_errno);
}

/* Preference lecteurs : une acquisition recursive en lecture ne bloque pas
 * derriere un ecrivain en attente. Pas de garantie de FIFO. */
int pthread_rwlock_rdlock(pthread_rwlock_t *lock) { return acquire(lock, 0, 0); }
int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock) { return acquire(lock, 0, 1); }
int pthread_rwlock_wrlock(pthread_rwlock_t *lock) { return acquire(lock, 1, 0); }
int pthread_rwlock_trywrlock(pthread_rwlock_t *lock) { return acquire(lock, 1, 1); }

int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
  if (!lock) return EINVAL;
  int saved_errno = errno;
  int error = pthread_mutex_lock(&lock->gate);
  if (error) { errno = saved_errno; return error; }
  if (lock->destroyed) return finish(lock, EINVAL, saved_errno);
  if (lock->writer) {
    if (lock->writer != (uint32_t)syscall0(SYS_GETTID))
      return finish(lock, EPERM, saved_errno);
    lock->writer = 0;
  } else if (lock->readers) --lock->readers;
  else return finish(lock, EINVAL, saved_errno);
  if (!lock->readers && lock->waiters)
    error = pthread_cond_broadcast(&lock->changed);
  return finish(lock, error, saved_errno);
}
