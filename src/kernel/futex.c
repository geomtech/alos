/* Attente privee UP : comparaison/enqueue atomiques sous la gate INT 0x80. */
#include "futex.h"
#include "thread.h"
#include "process.h"
#include "uaccess.h"
#include "../include/errno.h"

typedef struct {
  process_t *process;
  uint32_t *address;
  unsigned users;
  wait_queue_t queue;
} futex_slot_t;
static futex_slot_t slots[128];

static futex_slot_t *find_slot(uint32_t *address, int allocate) {
  futex_slot_t *free_slot = NULL;
  for (unsigned i = 0; i < 128; i++) {
    if (slots[i].users && slots[i].process == process_current() &&
        slots[i].address == address) return &slots[i];
    if (!slots[i].users && !free_slot) free_slot = &slots[i];
  }
  if (allocate && free_slot) {
    free_slot->process = process_current();
    free_slot->address = address;
    wait_queue_init(&free_slot->queue);
    return free_slot;
  }
  return NULL;
}

int futex_wait_private(uint32_t *address, uint32_t expected, uint32_t timeout_ms) {
  uint32_t value;
  if ((uint64_t)address & 3) return -EINVAL;
  if (copy_from_user(&value, address, sizeof(value))) return -EFAULT;
  if (value != expected) return -11; /* EAGAIN */
  futex_slot_t *slot = find_slot(address, 1);
  if (!slot) return -ENOMEM;
  slot->users++;
  int result = wait_queue_wait_timeout(&slot->queue, NULL, NULL, timeout_ms)
                   ? 0 : -ETIMEDOUT;
  slot->users--;
  return result;
}

int futex_wake_private(uint32_t *address, int count) {
  if (((uint64_t)address & 3) || count < 0) return -EINVAL;
  if (!user_range_valid(address, sizeof(*address), false)) return -EFAULT;
  futex_slot_t *slot = find_slot(address, 0);
  int woke = 0;
  while (slot && slot->queue.head && woke < count) {
    wait_queue_wake_one(&slot->queue);
    woke++;
  }
  return woke;
}
