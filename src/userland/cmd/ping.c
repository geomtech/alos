/* src/userland/cmd/ping.c - Send ICMP ECHO_REQUEST to network hosts */
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: ping <ip_address|hostname> [-c count]\n");
    printf("Examples:\n");
    printf("  ping 10.0.2.2        (continuous, press 'q' to stop)\n");
    printf("  ping google.com -c 1 (single ping)\n");
    return 1;
  }

  const char *target = argv[1];
  int count = 0; /* continuous by default */

  if (argc >= 4 && strcmp(argv[2], "-c") == 0) {
    if (strcmp(argv[3], "1") == 0) {
      count = 1;
    }
  }

  long ret = syscall2(SYS_PING, (long)target, count);
  return (ret == 0) ? 0 : 1;
}
