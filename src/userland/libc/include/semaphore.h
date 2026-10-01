#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
#include <stdint.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Semaphores POSIX anonymes, prives au processus (futex ALOS prives).
 * pshared != 0 et les semaphores nommes ne sont pas pris en charge. */
typedef struct {
  uint32_t value;
  uint32_t waiters;
} sem_t;

#define SEM_VALUE_MAX 0x7fffffff
#define SEM_FAILED ((sem_t *)0)

int sem_init(sem_t *semaphore, int pshared, unsigned int value);
int sem_destroy(sem_t *semaphore);
int sem_post(sem_t *semaphore);
int sem_wait(sem_t *semaphore);
int sem_trywait(sem_t *semaphore);
int sem_timedwait(sem_t *semaphore, const struct timespec *deadline);
int sem_getvalue(sem_t *semaphore, int *value);

#ifdef __cplusplus
}
#endif
#endif
