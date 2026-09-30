#ifndef THREAD_LIFECYCLE_H
#define THREAD_LIFECYCLE_H
#include "thread.h"
int thread_completion_create(process_t *process, thread_t *thread);
void thread_completion_finish(thread_t *thread);
void thread_completion_cleanup(process_t *process);
int thread_user_join(uint32_t tid);
int thread_user_detach(uint32_t tid);
int thread_user_register(uint32_t tid, uint64_t descriptor, uint64_t stack,
                         uint64_t stack_size, uint64_t descriptor_size);
#endif
