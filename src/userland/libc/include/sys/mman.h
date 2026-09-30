#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>
#include "../../../../include/mman.h"

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *address, size_t length, int prot, int flags, int fd,
            off_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int prot);
int madvise(void *address, size_t length, int advice);
int mincore(void *address, size_t length, unsigned char *vector);
int msync(void *address, size_t length, int flags);

#ifdef __cplusplus
}
#endif
#endif
