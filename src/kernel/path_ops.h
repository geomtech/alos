#ifndef KERNEL_PATH_OPS_H
#define KERNEL_PATH_OPS_H
#include <stdint.h>
int64_t native_path_call(int op, int dirfd, const char* path, uint64_t flags,
                         uint64_t argument, uint64_t count);
#endif
