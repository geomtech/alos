#ifndef ALOS_PIPE_H
#define ALOS_PIPE_H
#include "file.h"
#include "../include/native_io.h"
struct thread;
int file_table_pipe(file_descriptor_t table[MAX_FD], int output[2], int flags);
void pipe_release(open_file_description_t *description);
int64_t pipe_transfer_user(open_file_description_t *, void *, uint64_t, int);
int pipe_poll(open_file_description_t *, int events);
int pipe_bytes_available(open_file_description_t *);
int64_t pipe_transfer_vector(open_file_description_t *, const alos_iovec_t *,
                             int, uint64_t, int);
/* Appeler avant reclamation de la pile d'un thread abandonne dans un syscall. */
void pipe_thread_cleanup(struct thread *);
#endif
