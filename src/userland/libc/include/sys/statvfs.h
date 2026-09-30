#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H
#include "../../../../include/statvfs.h"
#ifdef __cplusplus
extern "C" {
#endif
int statvfs(const char *path, struct statvfs *information);
#ifdef __cplusplus
}
#endif
#endif
