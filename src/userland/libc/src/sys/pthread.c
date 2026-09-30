#include <pthread.h>
#include <sys/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#define STACK_SIZE (1024 * 1024)
#define STACK_GUARD 4096
#define STACK_LIMIT (16 * 1024 * 1024 - 2 * STACK_GUARD)
#define ATTR_LIVE 0x41545452u
struct alos_pthread {
  uint32_t tid, ready;
  void *(*entry)(void *);
  void *argument, *result, *stack;
  size_t stack_size, mapping_size;
  int detachstate;
};
static _Thread_local pthread_t self;
static _Thread_local void *specific[64];
static _Thread_local unsigned specific_generation[64];
static struct { unsigned generation, active; void (*destructor)(void *); } keys[64];
static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;

static int error_result(long result) { return result < 0 ? (int)-result : 0; }

static int attr_valid(const pthread_attr_t *attr) {
  return attr && attr->initialized == ATTR_LIVE;
}

int pthread_attr_init(pthread_attr_t *attr) {
  if (!attr) return EINVAL;
  *attr = (pthread_attr_t){NULL, STACK_SIZE, PTHREAD_CREATE_JOINABLE, ATTR_LIVE};
  return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
  if (!attr_valid(attr)) return EINVAL;
  *attr = (pthread_attr_t){0};
  return 0;
}

int pthread_attr_getstack(const pthread_attr_t *attr, void **address, size_t *size) {
  if (!attr_valid(attr) || !address || !size) return EINVAL;
  *address = attr->stack_address;
  *size = attr->stack_size;
  return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size) {
  if (!attr_valid(attr) || !size) return EINVAL;
  *size = attr->stack_size;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size) {
  if (!attr_valid(attr) || size < PTHREAD_STACK_MIN || size > STACK_LIMIT)
    return EINVAL;
  attr->stack_size = (size + STACK_GUARD - 1) & ~(size_t)(STACK_GUARD - 1);
  attr->stack_address = NULL;
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state) {
  if (!attr_valid(attr) || !state) return EINVAL;
  *state = attr->detachstate;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) {
  if (!attr_valid(attr) ||
      (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED))
    return EINVAL;
  attr->detachstate = state;
  return 0;
}

int pthread_getattr_np(pthread_t thread, pthread_attr_t *attr) {
  if (!thread || !attr) return EINVAL;
  void *address;
  size_t size;
  if (thread->stack) {
    address = (char *)thread->stack + STACK_GUARD;
    size = thread->stack_size;
  } else {
    uint64_t base, length;
    long result = syscall3(SYS_THREAD_STACK, thread->tid,
                            (long)&base, (long)&length);
    if (result < 0) return (int)-result;
    address = (void *)base;
    size = (size_t)length;
  }
  *attr = (pthread_attr_t){address, size,
                         __atomic_load_n(&thread->detachstate, __ATOMIC_ACQUIRE),
                         ATTR_LIVE};
  return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  if (!mutex) return EINVAL;
  if (attr && attr->type != PTHREAD_MUTEX_NORMAL &&
      attr->type != PTHREAD_MUTEX_RECURSIVE) return EINVAL;
  mutex->state = 0;
  mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
  mutex->owner = mutex->depth = 0;
  return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  return __atomic_load_n(&mutex->state, __ATOMIC_RELAXED) ? EBUSY : 0;
}
int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  uint32_t tid = 0;
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    tid = (uint32_t)syscall0(SYS_GETTID);
    if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == tid) {
      if (mutex->depth == UINT32_MAX) return EAGAIN;
      mutex->depth++;
      return 0;
    }
  }
  unsigned expected = 0;
  if (!__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                  __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return EBUSY;
  if (tid) {
    mutex->depth = 1;
    __atomic_store_n(&mutex->owner, tid, __ATOMIC_RELAXED);
  }
  return 0;
}
int pthread_mutex_lock(pthread_mutex_t *mutex) {
  int error = pthread_mutex_trylock(mutex);
  if (!error) return 0;
  if (error != EBUSY) return error;
  while (__atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE)) {
    if (futex_wait(&mutex->state, 2, 0) && errno != EAGAIN) return errno;
  }
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    mutex->depth = 1;
    __atomic_store_n(&mutex->owner, (uint32_t)syscall0(SYS_GETTID), __ATOMIC_RELAXED);
  }
  return 0;
}
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) !=
        (uint32_t)syscall0(SYS_GETTID)) return EINVAL;
    if (--mutex->depth) return 0;
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELAXED);
  }
  unsigned old = __atomic_exchange_n(&mutex->state, 0, __ATOMIC_RELEASE);
  if (!old) return EINVAL;
  if (old == 2 && futex_wake(&mutex->state, 1) < 0) return errno;
  return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
  if (!attr) return EINVAL;
  attr->type = PTHREAD_MUTEX_NORMAL;
  return 0;
}
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
  if (!attr) return EINVAL;
  attr->type = -1;
  return 0;
}
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
  if (!attr || (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE))
    return EINVAL;
  attr->type = type;
  return 0;
}

pthread_t pthread_self(void) {
  if (!self) {
    self = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (self == MAP_FAILED) {
      puts("pthread: cannot allocate main thread descriptor");
      _exit(134);
    }
    self->tid = (uint32_t)syscall0(SYS_GETTID);
    self->ready = 1;
  }
  self->tid = (uint32_t)syscall0(SYS_GETTID);
  return self;
}
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

void __pthread_key_fini(void) {
  for (unsigned round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++) {
    int invoked = 0;
    for (unsigned i = 0; i < 64; i++) {
      pthread_mutex_lock(&key_lock);
      void (*destructor)(void *) =
          keys[i].active && keys[i].generation == specific_generation[i]
              ? keys[i].destructor : NULL;
      pthread_mutex_unlock(&key_lock);
      void *value = specific[i];
      specific[i] = NULL;
      if (value && destructor) { destructor(value); invoked = 1; }
    }
    if (!invoked) break;
  }
}
void pthread_exit(void *result) {
  extern void __libc_thread_fini(void);
  pthread_t thread = pthread_self();
  __libc_thread_fini();
  thread->result = result;
  _exit(0);
}
static void start_thread(void *argument) {
  self = argument;
  unsigned ready;
  while (!(ready = __atomic_load_n(&self->ready, __ATOMIC_ACQUIRE))) {
    if (futex_wait(&self->ready, 0, 0) && errno != EAGAIN) _exit(134);
  }
  if (ready == 2) _exit(0);
  pthread_exit(self->entry(self->argument));
}
static int create_thread(pthread_t *out, const pthread_attr_t *attr,
                          void *(*entry)(void *), void *argument) {
  if (!out || !entry) return EINVAL;
  size_t stack_size = STACK_SIZE;
  int detachstate = PTHREAD_CREATE_JOINABLE;
  if (attr) {
    if (!attr_valid(attr) || attr->stack_size < PTHREAD_STACK_MIN ||
        attr->stack_size > STACK_LIMIT ||
        (attr->detachstate != PTHREAD_CREATE_JOINABLE &&
         attr->detachstate != PTHREAD_CREATE_DETACHED))
      return EINVAL;
    if (attr->stack_address) return ENOTSUP;
    stack_size = (attr->stack_size + STACK_GUARD - 1) &
                 ~(size_t)(STACK_GUARD - 1);
    detachstate = attr->detachstate;
  }
  size_t mapping_size = stack_size + 2 * STACK_GUARD;
  pthread_t thread = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (thread == MAP_FAILED) return EAGAIN;
  void *stack = mmap(NULL, mapping_size, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (stack == MAP_FAILED) { munmap(thread, 4096); return EAGAIN; }
  if (mprotect((char *)stack + STACK_GUARD, stack_size,
                 PROT_READ | PROT_WRITE)) {
    munmap(stack, mapping_size); munmap(thread, 4096); return EAGAIN;
  }
  thread->entry = entry;
  thread->argument = argument;
  thread->stack = stack;
  thread->stack_size = stack_size;
  thread->mapping_size = mapping_size;
  thread->detachstate = detachstate;
  long tid = syscall3(SYS_THREAD_CREATE, (long)start_thread,
                      (long)stack + STACK_GUARD + stack_size, (long)thread);
  if (tid < 0) {
    munmap(stack, mapping_size); munmap(thread, 4096); return EAGAIN;
  }
  thread->tid = (uint32_t)tid;
  long result = syscall5(SYS_THREAD_REGISTER, tid, (long)thread,
                         (long)stack, mapping_size, 4096);
  if (!result && detachstate == PTHREAD_CREATE_DETACHED)
    result = syscall1(SYS_THREAD_DETACH, tid);
  if (result < 0) {
    __atomic_store_n(&thread->ready, 2, __ATOMIC_RELEASE);
    futex_wake(&thread->ready, 1);
    if (syscall1(SYS_THREAD_JOIN, tid) < 0) {
      puts("pthread: cannot reap aborted thread");
      _exit(134);
    }
    munmap(stack, mapping_size); munmap(thread, 4096);
    return error_result(result);
  }
  uint32_t *ready = &thread->ready;
  *out = thread;
  __atomic_store_n(ready, 1, __ATOMIC_RELEASE);
  /* Un detache peut finir avant wake : son adresse n'est plus dereferencee ici. */
  futex_wake(ready, 1);
  return 0;
}

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                    void *(*entry)(void *), void *argument) {
  int saved_errno = errno;
  int result = create_thread(out, attr, entry, argument);
  errno = saved_errno;
  return result;
}

int pthread_join(pthread_t thread, void **result) {
  if (!thread) return EINVAL;
  int error = error_result(syscall1(SYS_THREAD_JOIN, thread->tid));
  if (error) return error;
  if (result) *result = thread->result;
  if (thread->stack) munmap(thread->stack, thread->mapping_size);
  munmap(thread, 4096);
  return 0;
}
int pthread_detach(pthread_t thread) {
  if (!thread) return EINVAL;
  /* Ne pas acceder au descriptor apres detach : le reaper peut le liberer. */
  int previous = __atomic_exchange_n(&thread->detachstate,
                                      PTHREAD_CREATE_DETACHED, __ATOMIC_ACQ_REL);
  int error = error_result(syscall1(SYS_THREAD_DETACH, thread->tid));
  if (error)
    __atomic_store_n(&thread->detachstate, previous, __ATOMIC_RELEASE);
  return error;
}

int pthread_condattr_init(pthread_condattr_t *attr) { attr->clock = CLOCK_REALTIME; return 0; }
int pthread_condattr_destroy(pthread_condattr_t *attr) {
  if (!attr) return EINVAL;
  attr->clock = -1;
  return 0;
}
int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
  if (clock != CLOCK_MONOTONIC && clock != CLOCK_REALTIME) return EINVAL;
  attr->clock = clock; return 0;
}
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
  if (!cond) return EINVAL;
  if (attr && attr->clock != CLOCK_MONOTONIC && attr->clock != CLOCK_REALTIME)
    return EINVAL;
  cond->sequence = cond->waiters = 0;
  cond->clock = attr ? attr->clock : CLOCK_REALTIME;
  return 0;
}
int pthread_cond_destroy(pthread_cond_t *cond) {
  return __atomic_load_n(&cond->waiters, __ATOMIC_RELAXED) ? EBUSY : 0;
}
static int condition_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline) {
  uint32_t timeout = 0;
  int expired = 0;
  if (deadline) {
    if (deadline->tv_sec < 0 || deadline->tv_nsec < 0 ||
        deadline->tv_nsec >= 1000000000) return EINVAL;
    struct timespec now;
    if (clock_gettime(cond->clock, &now)) return errno;
    if (deadline->tv_sec < now.tv_sec ||
        (deadline->tv_sec == now.tv_sec && deadline->tv_nsec <= now.tv_nsec))
      expired = 1;
    uint64_t seconds = expired ? 0 : (uint64_t)(deadline->tv_sec - now.tv_sec);
    if (seconds > UINT32_MAX / 1000) return EINVAL;
    int64_t nanoseconds = (int64_t)seconds * 1000000000 +
                          deadline->tv_nsec - now.tv_nsec;
    if (!expired) timeout = (uint32_t)((nanoseconds + 999999) / 1000000);
  }
  uint32_t sequence = __atomic_load_n(&cond->sequence, __ATOMIC_ACQUIRE);
  __atomic_add_fetch(&cond->waiters, 1, __ATOMIC_RELAXED);
  int error = pthread_mutex_unlock(mutex);
  if (!error) {
    if (expired) error = ETIMEDOUT;
    else if (futex_wait(&cond->sequence, sequence, timeout) && errno != EAGAIN)
      error = errno;
  }
  __atomic_sub_fetch(&cond->waiters, 1, __ATOMIC_RELAXED);
  int lock_error = pthread_mutex_lock(mutex);
  return error ? error : lock_error;
}
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  return condition_wait(cond, mutex, NULL);
}
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline) {
  if (!deadline) return EINVAL;
  return condition_wait(cond, mutex, deadline);
}
static int signal_condition(pthread_cond_t *cond, int count) {
  __atomic_add_fetch(&cond->sequence, 1, __ATOMIC_RELEASE);
  return futex_wake(&cond->sequence, count) < 0 ? errno : 0;
}
int pthread_cond_signal(pthread_cond_t *cond) { return signal_condition(cond, 1); }
int pthread_cond_broadcast(pthread_cond_t *cond) { return signal_condition(cond, 0x7fffffff); }
int pthread_once(pthread_once_t *once, void (*function)(void)) {
  unsigned expected = 0;
  if (__atomic_compare_exchange_n(once, &expected, 1, 0,
                                  __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
    function();
    __atomic_store_n(once, 2, __ATOMIC_RELEASE);
    futex_wake(once, 0x7fffffff);
  } else while (__atomic_load_n(once, __ATOMIC_ACQUIRE) != 2) {
    if (futex_wait(once, 1, 0) && errno != EAGAIN) return errno;
  }
  return 0;
}
int pthread_key_create(pthread_key_t *out, void (*destructor)(void *)) {
  if (!out) return EINVAL;
  pthread_mutex_lock(&key_lock);
  for (unsigned i = 0; i < 64; i++) {
    if (!keys[i].active && keys[i].generation < 0x3ffffff) {
      keys[i].generation++;
      keys[i].active = 1;
      keys[i].destructor = destructor;
      *out = (keys[i].generation << 6) | i;
      pthread_mutex_unlock(&key_lock);
      return 0;
    }
  }
  pthread_mutex_unlock(&key_lock);
  return EAGAIN;
}
int pthread_key_delete(pthread_key_t key) {
  unsigned i = key & 63;
  pthread_mutex_lock(&key_lock);
  int error = !keys[i].active || keys[i].generation != (key >> 6) ? EINVAL : 0;
  if (!error) keys[i].active = 0;
  pthread_mutex_unlock(&key_lock);
  return error;
}
int pthread_setspecific(pthread_key_t key, const void *value) {
  unsigned i = key & 63;
  pthread_mutex_lock(&key_lock);
  int error = !keys[i].active || keys[i].generation != (key >> 6) ? EINVAL : 0;
  if (!error) { specific_generation[i] = key >> 6; specific[i] = (void *)value; }
  pthread_mutex_unlock(&key_lock);
  return error;
}
void *pthread_getspecific(pthread_key_t key) {
  unsigned i = key & 63;
  pthread_mutex_lock(&key_lock);
  void *value = keys[i].active && keys[i].generation == (key >> 6) &&
                  specific_generation[i] == (key >> 6) ? specific[i] : NULL;
  pthread_mutex_unlock(&key_lock);
  return value;
}
