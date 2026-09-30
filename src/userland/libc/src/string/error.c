#include <string.h>
#include <errno.h>

static char *message(int error) {
  switch (error) {
  case 0: return "Success";
  case ESRCH: return "No such process";
  case EINTR: return "Interrupted system call";
  case EIO: return "Input/output error";
  case EBADF: return "Bad file descriptor";
  case EAGAIN: return "Resource temporarily unavailable";
  case ENOMEM: return "Cannot allocate memory";
  case EACCES: return "Permission denied";
  case EFAULT: return "Bad address";
  case EBUSY: return "Device or resource busy";
  case EEXIST: return "File exists";
  case EINVAL: return "Invalid argument";
  case ENOTTY: return "Inappropriate ioctl for device";
  case ERANGE: return "Numerical result out of range";
  case EDEADLK: return "Resource deadlock avoided";
  case ENOTSUP: return "Operation not supported";
  case ETIMEDOUT: return "Connection timed out";
  default: return NULL;
  }
}
char *strerror(int error) {
  char *text = message(error);
  return text ? text : "Unknown error";
}
int strerror_r(int error, char *buffer, size_t size) {
  char *text = message(error);
  if (!buffer || !size) return ERANGE;
  if (!text) { buffer[0] = 0; return EINVAL; }
  size_t length = strlen(text);
  if (length >= size) { buffer[0] = 0; return ERANGE; }
  memcpy(buffer, text, length + 1);
  return 0;
}
