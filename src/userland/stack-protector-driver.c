#include <stdio.h>
#include <string.h>
#include <sys/alos_process_control.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int unavailable = argc == 2 && !strcmp(argv[1], "--unavailable");
  if (argc != 1 && !unavailable) return 1;
  int child = fork();
  if (child < 0) return 1;
  if (!child) {
    char *arguments[] = {"stack-protector-test", NULL};
    char *environment[] = {NULL};
    execve("/bin/stack-protector-test", arguments, environment);
    alos_process_exit(127);
  }
  alos_process_exit_t result;
  if (alos_process_wait(child, &result, ALOS_PROCESS_WAIT_FOREVER) != child ||
      result.raw_status != (unavailable ? 134 : 0) ||
      result.reason != ALOS_PROCESS_EXIT_NORMAL) {
    puts("stack-protector-driver: FAIL");
    return 1;
  }
  puts(unavailable ? "stack-protector-driver: unavailable PASS" :
                     "stack-protector-driver: PASS");
  return 0;
}
