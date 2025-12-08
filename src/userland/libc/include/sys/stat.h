#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

/* File mode bits */
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

#endif
