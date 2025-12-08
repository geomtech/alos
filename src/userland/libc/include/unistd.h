#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int unlink(const char *pathname);
off_t lseek(int fd, off_t offset, int whence);
int getpid(void);
int getuid(void);
unsigned int sleep(unsigned int seconds);
int chdir(const char *path);
int rmdir(const char *pathname);
void _exit(int status);

#endif
