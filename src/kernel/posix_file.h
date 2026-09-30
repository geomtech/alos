#ifndef ALOS_POSIX_FILE_H
#define ALOS_POSIX_FILE_H
#include <stdint.h>
int64_t posix_fsync(int fd);
int64_t posix_pipe2(int* output, int flags);
int64_t posix_access(const char* path, int mode);
int64_t posix_mkdir(const char* path, uint32_t mode);
int64_t posix_pread(int fd, void* buffer, uint64_t count, int64_t offset);
int64_t posix_pwrite(int fd, const void* buffer, uint64_t count, int64_t offset);
int64_t posix_ftruncate(int fd, int64_t length);
#endif
