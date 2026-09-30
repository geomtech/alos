#ifndef _FCNTL_H
#define _FCNTL_H

#include "../../../include/fcntl.h"
#include <sys/types.h>

struct flock {
  short l_type;
  short l_whence;
  off_t l_start;
  off_t l_len;
  pid_t l_pid;
};

#ifdef __cplusplus
extern "C" {
#endif
int open(const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...);
int fcntl(int fd, int command, ...);

/**
 * creat() - Crée un fichier vide via SYS_CREATE.
 *
 * `mode` est accepté pour compatibilité avec la signature POSIX standard
 * mais ignoré (pas de permissions implémentées).
 */
int creat(const char *pathname, int mode);

#ifdef __cplusplus
}
#endif
#endif
