/* src/kernel/display.c - Propriete exclusive du serveur d'affichage */
#include "display.h"

static volatile uint32_t g_display_owner;

void display_init(void) { g_display_owner = 0; }

int display_acquire(uint32_t pid) {
  if (pid == 0) {
    return -1;
  }
  uint32_t expected = 0;
  if (__atomic_compare_exchange_n(&g_display_owner, &expected, pid, false,
                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ||
      expected == pid) {
    return 0;
  }
  return -1;
}

int display_release(uint32_t pid) {
  uint32_t expected = pid;
  return __atomic_compare_exchange_n(&g_display_owner, &expected, 0, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
             ? 0
             : -1;
}

void display_release_if_owner(uint32_t pid) { (void)display_release(pid); }

bool display_is_owner(uint32_t pid) {
  return pid != 0 && __atomic_load_n(&g_display_owner, __ATOMIC_SEQ_CST) == pid;
}
