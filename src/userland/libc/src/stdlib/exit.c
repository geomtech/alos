/* src/userland/libc/src/stdlib/exit.c */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>

extern void __libc_fini(void);
void exit(int status) {
  __libc_fini();
  _exit(status);
}

void abort(void) {
  puts("libc: abort (SIGABRT)");
  syscall1(SYS_EXIT_GROUP, 134);
  __builtin_unreachable();
}
