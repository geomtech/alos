#ifndef ALOS_UNIX_SOCKET_H
#define ALOS_UNIX_SOCKET_H
#include "../fs/file.h"
#include "../include/native_socket.h"

int64_t unix_socket_pair(int family, int type, int protocol, void *output);
int64_t unix_socket_call(open_file_description_t *, uint64_t operation,
                         uint64_t b, uint64_t c, uint64_t d, uint64_t e);
int unix_socket_poll(open_file_description_t *, short events);
void unix_socket_release(struct unix_socket *);
int64_t unix_socket_read(open_file_description_t *, void *, uint64_t, int);
int64_t unix_socket_write(open_file_description_t *, const void *, uint64_t, int);
#endif
