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

  /* Collapse consecutive mouse moves to the newest coordinates. */
  if (event->type == EVENT_MOUSE_MOVE && g_head != g_tail) {
    uint32_t previous = (g_head + MAX_EVENTS - 1) % MAX_EVENTS;
    if (g_event_queue[previous].type == EVENT_MOUSE_MOVE) {
      g_event_queue[previous] = *event;
      spinlock_irqrestore(&g_input_lock, flags);
      return;
    }
  }

  uint32_t next = (g_head + 1) % MAX_EVENTS;
  if (next != g_tail) {
    g_event_queue[g_head] = *event;
    g_head = next;
  } else {
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

int input_wait_event(input_event_t *event, uint32_t timeout_ms) {
  if (!event)
    return -1;

  /* Essayer de pop immédiatement */
  if (input_pop_event(event) == 1)
    return 1;

  /* Calculer le tick de timeout */
  extern uint64_t timer_get_ticks(void);
  extern void thread_yield(void);
  uint64_t deadline = 0;
  if (timeout_ms > 0) {
    deadline = timer_get_ticks() + timeout_ms;
  }

  /* Boucle poll + yield : céder le CPU entre chaque vérification.
   * C'est sûr car thread_yield() n'est jamais appelé depuis un ISR. */
  while (1) {
    thread_yield();

    if (input_pop_event(event) == 1)
      return 1;

    /* Vérifier le timeout */
    if (deadline > 0 && timer_get_ticks() >= deadline)
      return 0;  /* Timeout */
  }
}
