#include <errno.h>
#include <sys/alos_system.h>
#include <sys/syscall.h>

int alos_system_info(alos_system_info_t *info) {
  long result = syscall3(SYS_SYSTEM_INFO, (long)info,
                         sizeof(alos_system_info_t), ALOS_SYSTEM_INFO_VERSION);
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return (int)result;
}
