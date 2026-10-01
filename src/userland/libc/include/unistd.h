#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF 31
#define _SC_NPROCESSORS_ONLN 32
#define _SC_PHYS_PAGES 33
#define _PC_NAME_MAX 1
#define _PC_PATH_MAX 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int fd);
int dup2(int fd, int target);
int unlink(const char *pathname);
int unlinkat(int dirfd, const char *path, int flags);
int symlink(const char *target, const char *path);
int link(const char *old_path, const char *new_path);
ssize_t readlink(const char *path, char *buffer, size_t size);
long pathconf(const char *path, int name);
off_t lseek(int fd, off_t offset, int whence);
int getpid(void);
pid_t gettid(void);
pid_t fork(void);
int execve(const char *pathname, char *const argv[], char *const envp[]);
int getuid(void);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t microseconds);
int fsync(int fd);
int pipe(int output[2]);
int pipe2(int output[2], int flags);
int access(const char *path, int mode);
int getentropy(void *buffer, size_t length);
ssize_t pread(int fd, void *buffer, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset);
int ftruncate(int fd, off_t length);
int ftruncate64(int fd, off_t length);
int truncate(const char *path, off_t length);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int mkdir(const char *pathname, ...);
int rmdir(const char *pathname);
int spawn_wait(const char *path, int argc, char **argv);
void _exit(int status) __attribute__((noreturn));
long sysconf(int name);
int getpagesize(void);
int isatty(int fd);

#ifdef __cplusplus
}
#endif
#endif
