/* src/userland/crash-test.c - Test utility for userland exception handling in ALOS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
  printf("ALOS Userland Crash Test Utility\n");
  printf("Usage: crash-test <type>\n");
  printf("Types:\n");
  printf("  null     - Dereference NULL pointer (*(int*)0 = 42) -> Page Fault (SIGSEGV)\n");
  printf("  divzero  - Integer division by zero (1 / 0) -> Division By Zero (SIGFPE)\n");
  printf("  illegal  - Execute invalid opcode (ud2) -> Invalid Opcode (SIGILL)\n");
  printf("  gpf      - Privileged instruction in Ring 3 (cli) -> GPF (SIGSEGV)\n");
  printf("  badread  - Read unmapped non-canonical address -> Page Fault (SIGSEGV)\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  const char *type = argv[1];

  if (strcmp(type, "null") == 0) {
    printf("[crash-test] Triggering NULL pointer dereference (write)...\n");
    *(volatile int *)0 = 42;
  } else if (strcmp(type, "divzero") == 0) {
    printf("[crash-test] Triggering division by zero...\n");
    volatile int zero = 0;
    volatile int result = 42 / zero;
    (void)result;
  } else if (strcmp(type, "illegal") == 0) {
    printf("[crash-test] Triggering invalid opcode (ud2)...\n");
    __asm__ volatile("ud2");
  } else if (strcmp(type, "gpf") == 0) {
    printf("[crash-test] Triggering privileged instruction in Ring 3 (cli)...\n");
    __asm__ volatile("cli");
  } else if (strcmp(type, "badread") == 0) {
    printf("[crash-test] Triggering read from unmapped memory...\n");
    volatile int val = *(volatile int *)0x00007FFFF0000000ULL;
    (void)val;
  } else {
    printf("Unknown test type: %s\n", type);
    print_usage();
    return 1;
  }

  printf("[crash-test] ERROR: Should not reach here!\n");
  return 0;
}
