#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  volatile int private_value = 10;
  pid_t child = fork();

  if (child < 0) {
    puts("fork-test: fork failed");
    return 1;
  }

  if (child == 0) {
    private_value = 20;
    printf("fork-test: child pid=%d value=%d\n", getpid(), private_value);
    return 42;
  }

  private_value = 30;
  int status = -1;
  pid_t waited = waitpid(child, &status, 0);
  printf("fork-test: parent pid=%d child=%d waited=%d value=%d status=%d\n",
         getpid(), child, waited, private_value, status);

  if (waited != child || private_value != 30 || status != 42) {
    puts("fork-test: FAIL");
    return 1;
  }

  puts("fork-test: PASS");
  return 0;
}
