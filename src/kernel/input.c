#include "input.h"
#include "../include/string.h"
#include "klog.h"
#include "syscall.h" /* For input_event_t */
#include "thread.h"  /* For spinlock_t */

#define MAX_EVENTS 256

static input_event_t g_event_queue[MAX_EVENTS];
static uint32_t g_head = 0;
static uint32_t g_tail = 0;
static spinlock_t g_input_lock;

void input_init(void) {
  g_head = 0;
  g_tail = 0;
  spinlock_init(&g_input_lock);
  KLOG_INFO("INPUT", "Input subsystem initialized");
}

void input_push_event(input_event_t *event) {
  if (!event)
    return;

  uint64_t flags = spinlock_irqsave(&g_input_lock);

  uint32_t next = (g_head + 1) % MAX_EVENTS;
  if (next != g_tail) {
    g_event_queue[g_head] = *event;
    g_head = next;
  } else {
    /* Drop event if full */
    KLOG_WARN("INPUT", "Event queue full, dropping event");
  }

  spinlock_irqrestore(&g_input_lock, flags);
}

int input_pop_event(input_event_t *event) {
  if (!event)
    return 0;

  uint64_t flags = spinlock_irqsave(&g_input_lock);

  if (g_head == g_tail) {
    spinlock_irqrestore(&g_input_lock, flags);
    return 0; /* Empty */
  }

  *event = g_event_queue[g_tail];
  g_tail = (g_tail + 1) % MAX_EVENTS;

  spinlock_irqrestore(&g_input_lock, flags);
  return 1;
}
