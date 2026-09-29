#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <stddef.h>

int shm_create(size_t size);
void *shm_map(int fd);
int shm_unmap(void *address);
long shm_size(int fd);

#endif
