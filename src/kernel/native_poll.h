#ifndef KERNEL_NATIVE_POLL_H
#define KERNEL_NATIVE_POLL_H
#include <stdint.h>
#include "thread.h"
void native_poll_notify(void);
wait_queue_t *native_poll_waitqueue(void);
int64_t native_poll(void *, uint64_t, int);
void native_poll_thread_cleanup(thread_t *);
#endif
