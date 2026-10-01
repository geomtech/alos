#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define PRODUCERS 2
#define CONSUMERS 2
#define ITEMS 2000

static sem_t items;
static sem_t slots;
static uint32_t consumed;

static void *producer(void *argument) {
  (void)argument;
  for (int i = 0; i < ITEMS; ++i) {
    if (sem_wait(&slots) || sem_post(&items)) return (void *)1;
  }
  return NULL;
}

static void *consumer(void *argument) {
  (void)argument;
  for (int i = 0; i < ITEMS; ++i) {
    if (sem_wait(&items)) return (void *)1;
    __atomic_add_fetch(&consumed, 1, __ATOMIC_RELAXED);
    if (sem_post(&slots)) return (void *)1;
  }
  return NULL;
}

static sem_t handoff;
static void *late_poster(void *argument) {
  (void)argument;
  struct timespec delay = {0, 50000000};
  nanosleep(&delay, NULL);
  return (void *)(intptr_t)sem_post(&handoff);
}

static int fail(const char *what) {
  printf("semaphore-test: FAIL %s errno=%d\n", what, errno);
  return 1;
}

int main(void) {
  sem_t local;
  int value = -1;
  if (sem_init(&local, 1, 0) == 0 || errno != ENOSYS) return fail("pshared");
  if (sem_init(&local, 0, (unsigned)SEM_VALUE_MAX + 1u) == 0 || errno != EINVAL)
    return fail("range");
  if (sem_init(&local, 0, 1)) return fail("init");
  if (sem_trywait(&local)) return fail("trywait available");
  if (sem_trywait(&local) == 0 || errno != EAGAIN) return fail("trywait empty");

  struct timespec start, deadline, end;
  clock_gettime(CLOCK_REALTIME, &start);
  deadline = start;
  deadline.tv_nsec += 100000000;
  if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
  if (sem_timedwait(&local, &deadline) == 0 || errno != ETIMEDOUT) return fail("timeout");
  clock_gettime(CLOCK_REALTIME, &end);
  int64_t elapsed = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
  if (elapsed < 90) return fail("timeout too early");
  struct timespec bad = {0, 1000000000};
  if (sem_timedwait(&local, &bad) == 0 || errno != EINVAL) return fail("einval");
  if (sem_post(&local) || sem_getvalue(&local, &value) || value != 1) return fail("getvalue");
  if (sem_destroy(&local)) return fail("destroy");

  /* Reveil d'un waiter bloque par un post tardif. */
  pthread_t thread;
  if (sem_init(&handoff, 0, 0) || pthread_create(&thread, NULL, late_poster, NULL))
    return fail("handoff setup");
  if (sem_wait(&handoff)) return fail("handoff wait");
  void *result;
  if (pthread_join(thread, &result) || result) return fail("handoff join");

  /* Producteurs/consommateurs bornes : 4 places. */
  pthread_t threads[PRODUCERS + CONSUMERS];
  if (sem_init(&items, 0, 0) || sem_init(&slots, 0, 4)) return fail("pc init");
  for (int i = 0; i < PRODUCERS; ++i)
    if (pthread_create(&threads[i], NULL, producer, NULL)) return fail("create");
  for (int i = 0; i < CONSUMERS; ++i)
    if (pthread_create(&threads[PRODUCERS + i], NULL, consumer, NULL)) return fail("create");
  for (int i = 0; i < PRODUCERS + CONSUMERS; ++i)
    if (pthread_join(threads[i], &result) || result) return fail("worker");
  if (consumed != PRODUCERS * ITEMS) return fail("count");
  if (sem_getvalue(&items, &value) || value != 0) return fail("items left");
  if (sem_getvalue(&slots, &value) || value != 4) return fail("slots left");
  printf("semaphore-test: PASS\n");
  return 0;
}
