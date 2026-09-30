#ifndef ALOS_STATVFS_ABI_H
#define ALOS_STATVFS_ABI_H
#include <stdint.h>
#define ST_RDONLY 1U
#define ST_NOSUID 2U
struct statvfs {
  uint64_t f_bsize, f_frsize;
  uint64_t f_blocks, f_bfree, f_bavail;
  uint64_t f_files, f_ffree, f_favail;
  uint64_t f_fsid, f_flag, f_namemax;
};
#endif
