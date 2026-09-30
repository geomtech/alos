#include "system_info.h"
#include "thread.h"
#include "uaccess.h"
#include "klog.h"
#include "../include/errno.h"
#include "../mm/pmm.h"

int sys_system_info(alos_system_info_t *destination, size_t size,
                    unsigned version) {
  if (size != sizeof(alos_system_info_t) ||
      version != ALOS_SYSTEM_INFO_VERSION)
    return -EINVAL;
  preempt_disable();
  alos_system_info_t info = {
      ALOS_SYSTEM_INFO_VERSION, sizeof(alos_system_info_t),
      ALOS_SYSTEM_INFO_MEMORY_FIELDS,
      pmm_get_usable_blocks() * PMM_BLOCK_SIZE,
      pmm_get_free_blocks() * PMM_BLOCK_SIZE, 0, 0};
  int result;
  if (!info.managed_total_bytes ||
      info.allocatable_free_bytes > info.managed_total_bytes) {
    KLOG_ERROR("SYSTEM_INFO", "Invalid PMM memory accounting");
    result = -EIO;
  } else {
    result = copy_to_user(destination, &info, sizeof(info)) ? -EFAULT : 0;
  }
  preempt_enable();
  return result;
}
