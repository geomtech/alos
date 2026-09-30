#ifndef _PTHREAD_H
#define _PTHREAD_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct alos_pthread *pthread_t;
typedef unsigned pthread_key_t;
typedef uint32_t pthread_once_t;
typedef struct { uint32_t state; int type; uint32_t owner, depth; } pthread_mutex_t;
typedef struct { uint32_t sequence, waiters; clockid_t clock; } pthread_cond_t;
typedef struct {
  void *stack_address;
  size_t stack_size;
  int detachstate;
  uint32_t initialized;
} pthread_attr_t;
typedef struct { int type; } pthread_mutexattr_t;
typedef struct { clockid_t clock; } pthread_condattr_t;
typedef struct {
  pthread_mutex_t gate;
  pthread_cond_t changed;
  uint32_t readers, writer, waiters, destroyed;
} pthread_rwlock_t;
typedef struct { int reserved; } pthread_rwlockattr_t;
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_STACK_MIN 16384
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_INITIALIZER {0, PTHREAD_MUTEX_NORMAL, 0, 0}
#define PTHREAD_COND_INITIALIZER {0, 0, CLOCK_REALTIME}
#define PTHREAD_RWLOCK_INITIALIZER {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, 0}
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_attr_getstack(const pthread_attr_t *, void **, size_t *);
int pthread_attr_getstacksize(const pthread_attr_t *, size_t *);
int pthread_attr_setstacksize(pthread_attr_t *, size_t);
int pthread_attr_getdetachstate(const pthread_attr_t *, int *);
int pthread_attr_setdetachstate(pthread_attr_t *, int);
/* Piles pthread gerees par libc et pile principale ELF ; pas de pile externe. */
int pthread_getattr_np(pthread_t, pthread_attr_t *);
int pthread_join(pthread_t, void **);
int pthread_detach(pthread_t);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
void pthread_exit(void *) __attribute__((noreturn));
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutexattr_init(pthread_mutexattr_t *);
int pthread_mutexattr_destroy(pthread_mutexattr_t *);
int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_condattr_init(pthread_condattr_t *);
int pthread_condattr_destroy(pthread_condattr_t *);
int pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int pthread_once(pthread_once_t *, void (*)(void));
int pthread_rwlock_init(pthread_rwlock_t *, const pthread_rwlockattr_t *);
int pthread_rwlock_destroy(pthread_rwlock_t *);
int pthread_rwlock_rdlock(pthread_rwlock_t *);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *);
int pthread_rwlock_wrlock(pthread_rwlock_t *);
int pthread_rwlock_trywrlock(pthread_rwlock_t *);
int pthread_rwlock_unlock(pthread_rwlock_t *);
int pthread_key_create(pthread_key_t *, void (*)(void *));
int pthread_key_delete(pthread_key_t);
int pthread_setspecific(pthread_key_t, const void *);
void *pthread_getspecific(pthread_key_t);
#ifdef __cplusplus
}
#endif
#endif
