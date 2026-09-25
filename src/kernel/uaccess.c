/* src/kernel/uaccess.c - Acces controles a la memoire userland */
#include "uaccess.h"
#include "../include/memlayout.h"
#include "../include/string.h"
#include "../mm/vmm.h"
#include "process.h"
#include <stdint.h>

static page_directory_t *current_user_directory(void) {
  process_t *process = process_current();
  if (process == NULL || process->pml4 == NULL) {
    return NULL;
  }
  return (page_directory_t *)process->pml4;
}

bool user_range_valid(const void *address, size_t size, bool write) {
  if (size == 0) {
    return true;
  }

  uint64_t start = (uint64_t)address;
  uint64_t end = start + size - 1;
  if (end < start || !is_user_address(start) || !is_user_address(end)) {
    return false;
  }

  page_directory_t *directory = current_user_directory();
  if (directory == NULL) {
    return false;
  }

  uint64_t page = PAGE_ALIGN_DOWN(start);
  uint64_t last_page = PAGE_ALIGN_DOWN(end);
  for (;;) {
    vmm_mapping_info_t mapping;
    if (vmm_query_mapping(directory, page, &mapping) != 0 ||
        !(mapping.raw_entry & PAGE_PRESENT) ||
        !(mapping.raw_entry & PAGE_USER) ||
        (write && !(mapping.raw_entry & PAGE_RW))) {
      return false;
    }
    if (page == last_page) {
      break;
    }
    page += PAGE_SIZE;
  }
  return true;
}

static int copy_user_pages(void *destination, const void *source, size_t size,
                           bool destination_is_user) {
  if (size == 0) {
    return 0;
  }
  if (destination == NULL || source == NULL) {
    return -1;
  }

  const void *user_address = destination_is_user ? destination : source;
  if (!user_range_valid(user_address, size, destination_is_user)) {
    return -1;
  }

  page_directory_t *directory = current_user_directory();
  uint8_t *kernel_bytes = destination_is_user ? (uint8_t *)source
                                               : (uint8_t *)destination;
  uint64_t user_cursor = (uint64_t)user_address;
  size_t remaining = size;

  while (remaining > 0) {
    uint64_t page_offset = user_cursor & (PAGE_SIZE - 1);
    size_t chunk = PAGE_SIZE - page_offset;
    if (chunk > remaining) {
      chunk = remaining;
    }

    uint64_t physical = vmm_get_phys_addr(directory, user_cursor);
    if (physical == 0) {
      return -1;
    }
    uint8_t *mapped = (uint8_t *)vmm_phys_to_virt(physical);

    if (destination_is_user) {
      memcpy(mapped, kernel_bytes, chunk);
    } else {
      memcpy(kernel_bytes, mapped, chunk);
    }

    kernel_bytes += chunk;
    user_cursor += chunk;
    remaining -= chunk;
  }
  return 0;
}

int copy_from_user(void *destination, const void *source, size_t size) {
  return copy_user_pages(destination, source, size, false);
}

int copy_to_user(void *destination, const void *source, size_t size) {
  return copy_user_pages(destination, source, size, true);
}

int copy_string_from_user(char *destination, const char *source,
                          size_t destination_size) {
  if (destination == NULL || source == NULL || destination_size == 0) {
    return -1;
  }

  for (size_t i = 0; i < destination_size; i++) {
    char value;
    if (copy_from_user(&value, source + i, 1) != 0) {
      destination[0] = '\0';
      return -1;
    }
    destination[i] = value;
    if (value == '\0') {
      return 0;
    }
  }

  destination[destination_size - 1] = '\0';
  return -1;
}
