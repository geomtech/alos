#include "resource.h"
#include "thread.h"
#include "uaccess.h"
#include "../fs/file.h"
#include "../include/errno.h"

int sys_resource_limit(int resource, alos_resource_limit_t *destination) {
  alos_resource_limit_t limit;
  switch (resource) {
  case ALOS_RESOURCE_NOFILE:
    limit.current = limit.maximum = MAX_FD;
    break;
  case ALOS_RESOURCE_DATA:
    /* Aucun quota DATA configurable ; les collisions d'adresses et OOM
     * restent des contraintes d'allocation, pas un rlimit. */
    limit.current = limit.maximum = ALOS_RESOURCE_INFINITY;
    break;
  default:
    return -ENOTSUP;
  }
  return copy_to_user(destination, &limit, sizeof(limit)) ? -EFAULT : 0;
}

int sys_resource_set_limit(int resource, const alos_resource_limit_t *source) {
  if (resource != ALOS_RESOURCE_NOFILE && resource != ALOS_RESOURCE_DATA)
    return -ENOTSUP;
  alos_resource_limit_t limit;
  if (copy_from_user(&limit, source, sizeof(limit)))
    return -EFAULT;
  if (limit.current > limit.maximum)
    return -EINVAL;
  return -ENOTSUP;
}

int sys_thread_nice(int operation, int value, int *destination) {
  if (operation != 0 && operation != 1)
    return -EINVAL;
  if (operation == 1 &&
      (value < THREAD_NICE_MIN || value > THREAD_NICE_MAX))
    return -EINVAL;
  preempt_disable();
  thread_t *thread = thread_current();
  int result = 0;
  if (!thread || !thread->owner) {
    result = -ESRCH;
  } else if (operation == 1) {
    /* Politique native actuelle : le thread peut modifier sa propre nice,
     * sans pretendre implementer les permissions uid/RLIMIT_NICE POSIX. */
    thread_set_nice(thread, (int8_t)value);
  } else {
    int nice = thread_get_nice(thread);
    result = copy_to_user(destination, &nice, sizeof(nice)) ? -EFAULT : 0;
  }
  preempt_enable();
  return result;
}
