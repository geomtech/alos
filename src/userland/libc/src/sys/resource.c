#include <errno.h>
#include <sys/alos_resource.h>
#include <sys/resource.h>
#include <sys/syscall.h>

static int checked(long result) {
  if (result < 0) {
    errno = (int)-result;
    return -1;
  }
  return (int)result;
}

int getrlimit(int resource, struct rlimit *limit) {
  if (!limit) {
    errno = EFAULT;
    return -1;
  }
  typedef char resource_limit_abi_check[
      sizeof(struct rlimit) == sizeof(alos_resource_limit_t) ? 1 : -1];
  (void)sizeof(resource_limit_abi_check);
  return checked(syscall2(SYS_RESOURCE_LIMIT, resource, (long)limit));
}

int setrlimit(int resource, const struct rlimit *limit) {
  if (!limit) {
    errno = EFAULT;
    return -1;
  }
  return checked(syscall2(SYS_RESOURCE_SET_LIMIT, resource, (long)limit));
}

int getrusage(int who, struct rusage *usage) {
  if (!usage) {
    errno = EFAULT;
    return -1;
  }
  typedef char rusage_abi_check[
      sizeof(struct rusage) == sizeof(alos_rusage_t) ? 1 : -1];
  (void)sizeof(rusage_abi_check);
  return checked(syscall2(SYS_RESOURCE_USAGE, who, (long)usage));
}

int getpriority(int which, id_t who) {
  (void)which;
  (void)who;
  errno = ENOTSUP;
  return -1;
}

int setpriority(int which, id_t who, int priority) {
  (void)which;
  (void)who;
  (void)priority;
  errno = ENOTSUP;
  return -1;
}

int alos_thread_get_nice(int *value) {
  return checked(syscall3(SYS_THREAD_NICE, 0, 0, (long)value));
}

int alos_thread_set_nice(int value) {
  return checked(syscall3(SYS_THREAD_NICE, 1, value, 0));
}

int alos_thread_can_set_nice(int value) {
  return value >= -20 && value <= 19;
}
