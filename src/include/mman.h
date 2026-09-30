/* ABI memoire partagee entre le noyau et la libc ALOS. */
#ifndef ALOS_MMAN_H
#define ALOS_MMAN_H

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *)-1)

#define MADV_DONTNEED 4

#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4

#endif
