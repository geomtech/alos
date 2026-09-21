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
static wait_queue_t g_input_wait_queue;

void input_init(void) {
  g_head = 0;
  g_tail = 0;
  spinlock_init(&g_input_lock);
  wait_queue_init(&g_input_wait_queue);
  KLOG_INFO("INPUT", "Input subsystem initialized");
}

void input_push_event(input_event_t *event) {
  if (!event)
    return;

  bool queued = false;
  uint64_t flags = spinlock_irqsave(&g_input_lock);

  /* Collapse consecutive mouse moves to the newest coordinates. */
  if (event->type == EVENT_MOUSE_MOVE && g_head != g_tail) {
    uint32_t previous = (g_head + MAX_EVENTS - 1) % MAX_EVENTS;
    if (g_event_queue[previous].type == EVENT_MOUSE_MOVE) {
      g_event_queue[previous] = *event;
      queued = true;
    }
  }

  if (!queued) {
    uint32_t next = (g_head + 1) % MAX_EVENTS;
    if (next != g_tail) {
      g_event_queue[g_head] = *event;
      g_head = next;
      queued = true;
    } else {
      KLOG_WARN("INPUT", "Event queue full, dropping event");
    }
  }

  spinlock_irqrestore(&g_input_lock, flags);

  if (queued)
    wait_queue_wake_all(&g_input_wait_queue);
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

static bool input_event_pending(void *context) {
  (void)context;
  uint64_t flags = spinlock_irqsave(&g_input_lock);
  bool pending = (g_head != g_tail);
  spinlock_irqrestore(&g_input_lock, flags);
  return pending;
}

int input_wait_event(input_event_t *event, uint32_t timeout_ms) {
  if (!event)
    return -1;

  if (input_pop_event(event) == 1)
    return 1;

  if (!wait_queue_wait_timeout(&g_input_wait_queue, input_event_pending, NULL,
                               timeout_ms))
    return 0;

  return input_pop_event(event);
}
