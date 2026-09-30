#ifndef KERNEL_NATIVE_SOCKET_H
#define KERNEL_NATIVE_SOCKET_H
#include "../fs/file.h"
#include "../include/native_socket.h"
int64_t native_socket_call(uint64_t op, uint64_t a, uint64_t b,
                           uint64_t c, uint64_t d, uint64_t e);
int64_t native_socket_read(open_file_description_t *, void *, uint64_t, int);
int64_t native_socket_write(open_file_description_t *, const void *, uint64_t, int);
short native_socket_poll(open_file_description_t *, short);
struct thread;
void native_socket_thread_cleanup(struct thread *);
#endif
