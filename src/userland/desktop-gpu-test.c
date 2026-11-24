/* Lance le desktop officiel (/bin/gui) puis un client (/bin/gui-demo) et rend la
 * main au shell en les laissant tourner : l'hote capture alors le scanout
 * virtio-gpu (test-vm.ps1 -DesktopGpuTest). */
#include <stdio.h>
#include <unistd.h>

static int spawn(const char *path) {
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    char *argv[] = {(char *)path, NULL};
    execve(path, argv, NULL);
    _exit(127);
  }
  return pid;
}

int main(void) {
  if (spawn("/bin/gui") < 0) { puts("desktop-gpu-test: FAIL spawn gui"); return 1; }
  sleep(3);
  if (spawn("/bin/gui-demo") < 0) { puts("desktop-gpu-test: FAIL spawn gui-demo"); return 1; }
  sleep(3);
  puts("desktop-gpu-test: PASS launched");
  return 0;
}
