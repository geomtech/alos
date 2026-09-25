/* src/userland/cmd/ps.c - List running processes and threads */
#include <stdio.h>
#include <sys/syscall.h>

static const char *state_to_str(uint32_t state) {
  switch (state) {
    case 0: return "READY";
    case 1: return "RUNNING";
    case 2: return "BLOCKED";
    case 3: return "SLEEPING";
    case 4: return "WAITING";
    case 5: return "ZOMBIE";
    default: return "UNKNOWN";
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  proc_info_t table[64];
  long count = syscall2(SYS_PS_INFO, (long)table, 64);
  if (count < 0) {
    printf("Error: failed to retrieve process list\n");
    return 1;
  }

  printf("\n=== Process / Thread List ===\n");
  printf("PID   TID   State       Name\n");
  printf("---   ---   -----       ----\n");

  for (long i = 0; i < count; i++) {
    printf("%-5u %-5u %-11s %s\n", table[i].pid, table[i].tid, state_to_str(table[i].state), table[i].name);
  }
  printf("\nTotal: %ld thread(s)\n\n", count);

  return 0;
}
