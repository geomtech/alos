#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct alos_directory_stream {
  int fd;
  struct dirent entry;
};

DIR *fdopendir(int fd) {
  struct stat metadata;
  if (fstat(fd, &metadata)) return NULL;
  if (!S_ISDIR(metadata.st_mode)) { errno = ENOTDIR; return NULL; }
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0) return NULL;
  if ((flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return NULL; }
  DIR *directory = malloc(sizeof(*directory));
  if (!directory) return NULL;
  memset(directory, 0, sizeof(*directory));
  directory->fd = fd;
  return directory;
}

DIR *opendir(const char *path) {
  int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return NULL;
  DIR *directory = fdopendir(fd);
  if (!directory) {
    int error = errno;
    close(fd);
    errno = error;
  }
  return directory;
}

static unsigned char directory_type(uint32_t type) {
  switch (type) {
    case 1: return DT_REG;
    case 2: return DT_DIR;
    case 3: return DT_CHR;
    case 4: return DT_BLK;
    case 5: return DT_FIFO;
    case 6: return DT_LNK;
    default: return DT_UNKNOWN;
  }
}

struct dirent *readdir(DIR *directory) {
  if (!directory) { errno = EBADF; return NULL; }
  alos_dir_record_t record;
  long result = syscall2(SYS_READDIR_FD, directory->fd, (long)&record);
  if (result < 0) { errno = (int)-result; return NULL; }
  if (!result) return NULL;
  directory->entry.d_ino = record.inode;
  directory->entry.d_off = (off_t)record.next_offset;
  directory->entry.d_reclen = sizeof(directory->entry);
  directory->entry.d_type = directory_type(record.type);
  memcpy(directory->entry.d_name, record.name, sizeof(record.name));
  return &directory->entry;
}

int closedir(DIR *directory) {
  if (!directory) { errno = EBADF; return -1; }
  int fd = directory->fd;
  free(directory);
  return close(fd);
}

int dirfd(DIR *directory) {
  if (!directory) { errno = EBADF; return -1; }
  return directory->fd;
}
