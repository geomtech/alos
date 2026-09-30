#include <errno.h>
#include <sys/alos_process_control.h>
#include <sys/syscall.h>
static int checked(long value) {
  if (value < 0) { errno = (int)-value; return -1; }
  return (int)value;
}
int alos_process_query(int pid, alos_process_info_t *information) {
  return checked(syscall2(SYS_PROCESS_QUERY, pid, (long)information));
}
int alos_process_terminate(int pid, int raw_status) {
  return checked(syscall2(SYS_PROCESS_TERMINATE, pid, raw_status));
}
int alos_process_wait(int pid, alos_process_exit_t *exit, uint32_t timeout_ms) {
  return checked(syscall3(SYS_PROCESS_WAIT, pid, (long)exit, timeout_ms));
}
void alos_process_exit(int raw_status) {
  syscall1(SYS_EXIT_GROUP, raw_status);
  __builtin_unreachable();
}
