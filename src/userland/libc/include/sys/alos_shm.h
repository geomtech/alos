#ifndef _SYS_ALOS_SHM_H
#define _SYS_ALOS_SHM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ALOS_SHM_MAX_SIZE (16U * 1024U * 1024U)
/* Descripteurs natifs immuables en taille, pas des fichiers VFS. */
int alos_shm_create(size_t size);
int alos_shm_readonly(int fd);
long alos_shm_size(int fd);
int alos_shm_access(int fd);
int alos_shm_same(int fd, int other_fd);
#ifdef __cplusplus
}
#endif
#endif
