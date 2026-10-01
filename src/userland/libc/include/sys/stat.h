#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>
#include <time.h>
#include "../../../../include/fs_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif

/* File mode bits */
#define S_IFMT 0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFBLK 0060000
#define S_IFREG 0100000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#define S_IRWXU 0700 /* RWX for owner */
#define S_IRUSR 0400 /* Read for owner */
#define S_IWUSR 0200 /* Write for owner */
#define S_IXUSR 0100 /* Execute for owner */
#define S_IRWXG 0070 /* RWX for group */
#define S_IRGRP 0040 /* Read for group */
#define S_IWGRP 0020 /* Write for group */
#define S_IXGRP 0010 /* Execute for group */
#define S_IRWXO 0007 /* RWX for others */
#define S_IROTH 0004 /* Read for others */
#define S_IWOTH 0002 /* Write for others */
#define S_IXOTH 0001 /* Execute for others */
#define UTIME_NOW 1073741823L
#define UTIME_OMIT 1073741822L

/**
 * mkdir() - Crée un répertoire via SYS_MKDIR.
 *
 * ALOS n'implémente pas encore de permissions : `mode` est accepté pour
 * compatibilité avec la signature POSIX standard mais ignoré.
 */
int mkdir(const char *pathname, ...);
int fstatat(int dirfd, const char *path, struct stat *output, int flags);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int fchmodat(int dirfd, const char *path, mode_t mode, int flags);
int futimens(int fd, const struct timespec times[2]);
int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);
int stat(const char *, struct stat *);
int lstat(const char *, struct stat *);
int fstat(int, struct stat *);

#ifdef __cplusplus
}
#endif
#endif
