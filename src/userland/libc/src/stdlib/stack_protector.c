#include <stdint.h>
#include <sys/alos_process_control.h>
#include <unistd.h>

uintptr_t __stack_chk_guard;

static void fail(const char *message, size_t length) __attribute__((noreturn));
static void fail(const char *message, size_t length) {
  write(STDERR_FILENO, message, length);
  alos_process_exit(134);
}

void __stack_chk_init(void) {
  uintptr_t guard;
  if (getentropy(&guard, sizeof(guard)) != 0 || !guard) {
    static const char message[] = "stack-protector: secure guard unavailable\n";
    fail(message, sizeof(message) - 1);
  }
  __stack_chk_guard = guard;
}

void __stack_chk_fail(void) __attribute__((noreturn));
void __stack_chk_fail(void) {
  static const char message[] = "stack-protector: stack corruption detected\n";
  fail(message, sizeof(message) - 1);
}
