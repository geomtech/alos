#ifndef ALOS_FS_METADATA_H
#define ALOS_FS_METADATA_H
#include <stdint.h>
#include "time.h"

/* ABI ALOS partagee noyau/libc ; timestamps Ext2 exprimes en secondes Unix. */
struct stat {
  uint64_t st_dev, st_ino, st_nlink;
  uint32_t st_mode, st_uid, st_gid, __reserved;
  uint64_t st_rdev;
  int64_t st_size, st_blksize, st_blocks;
  struct timespec st_atim, st_mtim, st_ctim;
};
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
#define ALOS_STAT_NOFOLLOW 1

/* type utilise les valeurs VFS, pas celles de d_type POSIX. */
typedef struct {
  uint64_t inode, next_offset;
  uint32_t type;
  char name[256];
  uint32_t reserved;
} alos_dir_record_t;

#ifdef __cplusplus
static_assert(sizeof(struct stat) == 120, "ALOS stat ABI");
static_assert(sizeof(alos_dir_record_t) == 280, "ALOS directory ABI");
#else
_Static_assert(sizeof(struct stat) == 120, "ALOS stat ABI");
_Static_assert(sizeof(alos_dir_record_t) == 280, "ALOS directory ABI");
#endif
#endif
