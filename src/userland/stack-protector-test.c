#include <stdint.h>
#include <stdio.h>
#include <sys/alos_process_control.h>
#include <unistd.h>

extern uintptr_t __stack_chk_guard;
static int preinitialized, constructed;

static void preinit(void) { preinitialized = __stack_chk_guard != 0; }
static void (*const preinit_entry)(void)
  __attribute__((section(".preinit_array"), used)) = preinit;
static void constructor(void) __attribute__((constructor));
static void constructor(void) { constructed = preinitialized && __stack_chk_guard; }

static void corrupt_guard(void) __attribute__((noinline));
static void corrupt_guard(void) {
  __stack_chk_guard ^= (uintptr_t)1;
  __asm__ volatile("" ::: "memory");
}

int main(void) {
  if (!constructed) {
    puts("stack-protector-test: FAIL initialization");
    return 1;
  }
  uintptr_t saved = __stack_chk_guard;
  int child = fork();
  if (child < 0) return 1;
  if (!child) {
    corrupt_guard();
    alos_process_exit(1);
  }
  alos_process_exit_t result;
  if (alos_process_wait(child, &result, ALOS_PROCESS_WAIT_FOREVER) != child ||
      result.raw_status != 134 || result.reason != ALOS_PROCESS_EXIT_NORMAL ||
      __stack_chk_guard != saved) {
    puts("stack-protector-test: FAIL corruption/fork");
    return 1;
  }
  puts("stack-protector-test: PASS");
  return 0;
}
