/* src/userland/libc/src/stdlib/exit.c */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/syscall.h>

extern void __libc_fini(void);
void exit(int status) {
  __libc_fini();
  /* exit() termine tout le processus (POSIX), threads compris ; _exit() reste
   * un exit de thread. Sans cela, un processus multithread dont main() retourne
   * ne devient jamais zombie et waitpid() bloque. */
  syscall1(SYS_EXIT_GROUP, status);
  __builtin_unreachable();
}

void abort(void) {
  puts("libc: abort (SIGABRT)");
  syscall1(SYS_EXIT_GROUP, 134);
  __builtin_unreachable();
}
