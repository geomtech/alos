#ifndef ALOS_NATIVE_EPOLL_H
#define ALOS_NATIVE_EPOLL_H

#include <stdint.h>
#include "../fs/file.h"
#include "../include/native_epoll.h"

int64_t native_epoll_call(uint64_t operation, uint64_t a, uint64_t b,
                          uint64_t c, uint64_t d);
int native_epoll_poll(open_file_description_t *description, short events);
void native_epoll_release(struct epoll_set *set);

#endif
