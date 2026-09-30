/* src/mm/vm.c - mmap sparse, protections et backing par processus */
#include "vm.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "../include/mman.h"
#include "../include/errno.h"
#include "../include/memlayout.h"
#include "../include/string.h"
#include "../kernel/process.h"
#include "../kernel/klog.h"
#include "../kernel/uaccess.h"
#include "../fs/vfs.h"

typedef struct vm_area {
  uint64_t start;
  uint64_t end;
  uint64_t offset;
  int prot;
  int max_prot;
  int flags;
  open_file_description_t *file;
  shm_object_t *shared;
  struct vm_area *next;
} vm_area_t;

static int protection_valid(int prot) {
  if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) return -EINVAL;
  if ((prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC))
    return -EACCES;
  return 0;
}

static bool range_valid(uint64_t address, uint64_t length, uint64_t *end) {
  if (address < USER_MMAP_BASE || address >= USER_MMAP_END ||
      (address & (PAGE_SIZE - 1)) || length == 0 ||
      length > USER_MMAP_END - address) return false;
  *end = address + PAGE_ALIGN_UP(length);
  return *end <= USER_MMAP_END;
}

static uint64_t page_flags(int prot) {
  uint64_t flags = PAGE_USER;
  if (prot != PROT_NONE) flags |= PAGE_PRESENT;
  if (prot & PROT_WRITE) flags |= PAGE_RW;
  if (!(prot & PROT_EXEC)) flags |= PAGE_NX;
  return flags;
}

static vm_area_t *find_area(const process_t *process, uint64_t address) {
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (address < area->start) break;
    if (address < area->end) return area;
  }
  return NULL;
}

static int page_residency(process_t *process, uint64_t address) {
  page_directory_t *directory = (page_directory_t *)process->pml4;
  vmm_mapping_info_t mapping;
  if (!vmm_query_mapping(directory, address, &mapping) &&
      (mapping.raw_entry & PAGE_USER)) return 1;
  uint64_t entry = vmm_get_page_entry(directory, address);
  /* PROT_NONE garde le frame mais retire PAGE_PRESENT. */
  if ((entry & PAGE_USER) && (entry & PAGE_FRAME_MASK)) return 1;
  return find_area(process, address) ? 0 : -ENOMEM;
}

int vm_msync(process_t *process, uint64_t address, uint64_t length, int flags) {
  if ((address & (PAGE_SIZE - 1)) ||
      (flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) ||
      ((flags & MS_ASYNC) && (flags & MS_SYNC)) ||
      !(flags & (MS_ASYNC | MS_SYNC))) return -EINVAL;
  if (!length) return 0;
  uint64_t pages = length / PAGE_SIZE + !!(length % PAGE_SIZE);
  if (!process || !process->pml4 || !is_user_address(address) ||
      pages > (UINT64_MAX - address) / PAGE_SIZE ||
      !is_user_address(address + pages * PAGE_SIZE - 1)) return -ENOMEM;
  uint64_t end = address + pages * PAGE_SIZE;
  for (uint64_t cursor = address; cursor < end;) {
    vm_area_t *area = find_area(process, cursor);
    if (area) {
      /* Aucun page-cache/writeback de fichier partage n'existe dans ALOS.
       * Les SHM sont des frames coherents ; les fichiers prives ne s'ecrivent
       * pas vers le backing. Leur invalidation n'est pas implemente ici. */
      if (area->file && ((area->flags & MAP_SHARED) ||
                         (flags & MS_INVALIDATE))) return -ENOTSUP;
      cursor = area->end < end ? area->end : end;
    } else {
      if (page_residency(process, cursor) < 0) return -ENOMEM;
      bool legacy_shared = false;
      for (unsigned i = 0; i < SHM_MAX_MAPPINGS; ++i) {
        const shm_process_mapping_t *mapping = &process->shm_mappings[i];
        if (mapping->object && cursor >= mapping->address &&
            cursor - mapping->address < mapping->size) {
          uint64_t mapping_end = mapping->address + mapping->size;
          cursor = mapping_end < end ? mapping_end : end;
          legacy_shared = true;
          break;
        }
      }
      /* Une PTE user ne prouve pas un backing RAM coherent : les mappings
       * externes peuvent notamment etre du framebuffer ou MMIO. */
      if (!legacy_shared) return -ENOTSUP;
    }
  }
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  return 0;
}

int vm_mincore(process_t *process, uint64_t address, uint64_t length,
                unsigned char *vector) {
  if (address & (PAGE_SIZE - 1)) return -EINVAL;
  if (!length) return 0;
  uint64_t pages = length / PAGE_SIZE + !!(length % PAGE_SIZE);
  if (!process || !process->pml4 || !is_user_address(address) ||
      pages > (UINT64_MAX - address) / PAGE_SIZE ||
      !is_user_address(address + pages * PAGE_SIZE - 1)) return -ENOMEM;
  if (!user_range_valid(vector, pages, true)) return -EFAULT;

  /* Ring 0 est cooperatif sur UP : aucune attente ni yield entre validation,
   * lecture des PTE/regions et copie. Ceci n'est pas un verrou SMP. */
  for (uint64_t i = 0; i < pages; ++i)
    if (page_residency(process, address + i * PAGE_SIZE) < 0) return -ENOMEM;
  unsigned char chunk[256];
  for (uint64_t offset = 0; offset < pages;) {
    size_t count = pages - offset < sizeof(chunk)
        ? (size_t)(pages - offset) : sizeof(chunk);
    for (size_t i = 0; i < count; ++i) {
      int resident = page_residency(process, address + (offset + i) * PAGE_SIZE);
      if (resident < 0) return resident;
      chunk[i] = (unsigned char)resident;
    }
    if (copy_to_user(vector + offset, chunk, count)) return -EFAULT;
    offset += count;
  }
  return 0;
}

static void retain_backing(vm_area_t *area) {
  file_description_retain(area->file);
  shm_retain(area->shared);
}

static void free_area(vm_area_t *area) {
  file_description_release(area->file);
  shm_release(area->shared);
  kfree(area);
}

/* Les deux allocations precedant les splits evitent une modification partielle. */
static int split_range(process_t *process, uint64_t start, uint64_t end) {
  vm_area_t *left = find_area(process, start);
  vm_area_t *right = find_area(process, end);
  vm_area_t *a = NULL;
  vm_area_t *b = NULL;
  if (left && left->start != start) {
    a = kmalloc(sizeof(*a));
    if (!a) return -ENOMEM;
  }
  if (right && right->start != end) {
    b = kmalloc(sizeof(*b));
    if (!b) {
      kfree(a);
      return -ENOMEM;
    }
  }
  if (a) {
    *a = *left;
    a->offset += start - left->start;
    a->start = start;
    left->end = start;
    left->next = a;
    retain_backing(a);
    if (right == left) right = a;
  }
  if (b) {
    *b = *right;
    b->offset += end - right->start;
    b->start = end;
    right->end = end;
    right->next = b;
    retain_backing(b);
  }
  return 0;
}

static bool range_covered(process_t *process, uint64_t start, uint64_t end) {
  uint64_t cursor = start;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (area->end <= cursor) continue;
    if (area->start > cursor) return false;
    cursor = area->end;
    if (cursor >= end) return true;
  }
  return false;
}

static void merge_areas(process_t *process) {
  vm_area_t *area = process->vm_areas;
  while (area && area->next) {
    vm_area_t *next = area->next;
    bool offset_matches = (!area->file && !area->shared) ||
        area->offset + area->end - area->start == next->offset;
    if (area->end == next->start && area->prot == next->prot &&
        area->max_prot == next->max_prot &&
        area->flags == next->flags && area->file == next->file &&
        area->shared == next->shared && offset_matches) {
      area->end = next->end;
      area->next = next->next;
      free_area(next);
    } else {
      area = next;
    }
  }
}

static void remove_range(process_t *process, uint64_t start, uint64_t end) {
  vm_area_t **link = &process->vm_areas;
  while (*link) {
    vm_area_t *area = *link;
    if (area->start >= start && area->end <= end) {
      vmm_update_range((page_directory_t *)process->pml4, area->start,
                        area->end, 0, true);
      *link = area->next;
      free_area(area);
    } else {
      link = &area->next;
    }
  }
}

static uint64_t find_gap(process_t *process, uint64_t hint, uint64_t size) {
  uint64_t cursor = hint;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (area->end <= cursor) continue;
    if (area->start > cursor && size <= area->start - cursor) return cursor;
    cursor = area->end;
  }
  return size <= USER_MMAP_END - cursor ? cursor : 0;
}

static uint64_t find_slot(process_t *process, uint64_t hint, uint64_t size) {
  uint64_t cursor = hint;
  while (cursor < USER_MMAP_END) {
    uint64_t address = find_gap(process, cursor, size);
    if (!address) return 0;
    uint64_t occupied = vmm_first_occupied_end(
        (page_directory_t *)process->pml4, address, address + size);
    if (!occupied) return address;
    cursor = occupied;
  }
  return 0;
}

static bool has_external_mapping(process_t *process, uint64_t start,
                                  uint64_t end) {
  page_directory_t *dir = (page_directory_t *)process->pml4;
  uint64_t cursor = start;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (area->end <= cursor) continue;
    if (area->start >= end) break;
    if (area->start > cursor &&
        vmm_first_occupied_end(dir, cursor, area->start)) return true;
    cursor = area->end;
    if (cursor >= end) return false;
  }
  return cursor < end && vmm_first_occupied_end(dir, cursor, end) != 0;
}

int64_t vm_mmap(process_t *process, uint64_t address, uint64_t length,
                 int prot, int flags, int fd, int64_t offset) {
  if (!process || !process->pml4) return -EINVAL;
  int error = protection_valid(prot);
  if (error) return error;
  int supported = MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED |
                  MAP_FIXED_NOREPLACE | MAP_NORESERVE;
  if (flags & ~supported) return -ENOTSUP;
  int kind = flags & (MAP_PRIVATE | MAP_SHARED);
  if (kind != MAP_PRIVATE && kind != MAP_SHARED) return -EINVAL;
  if (!length || length > USER_MMAP_END - USER_MMAP_BASE ||
      offset < 0 || ((uint64_t)offset & (PAGE_SIZE - 1))) return -EINVAL;
  uint64_t size = PAGE_ALIGN_UP(length);
  uint64_t end;
  bool fixed = flags & (MAP_FIXED | MAP_FIXED_NOREPLACE);
  if (fixed) {
    if (!range_valid(address, length, &end)) return -EINVAL;
    if ((flags & MAP_FIXED_NOREPLACE) &&
        find_gap(process, address, size) != address) return -EEXIST;
    if (has_external_mapping(process, address, end))
      return (flags & MAP_FIXED_NOREPLACE) ? -EEXIST : -EINVAL;
  } else {
    uint64_t hint = PAGE_ALIGN_DOWN(address);
    if (hint < USER_MMAP_BASE || hint >= USER_MMAP_END)
      hint = USER_MMAP_BASE;
    address = find_slot(process, hint, size);
    if (!address) address = find_slot(process, USER_MMAP_BASE, size);
    if (!address) return -ENOMEM;
    end = address + size;
  }

  vm_area_t *area = kmalloc(sizeof(*area));
  if (!area) return -ENOMEM;
  memset(area, 0, sizeof(*area));
  area->start = address;
  area->end = end;
  area->offset = (uint64_t)offset;
  area->prot = prot;
  area->max_prot = PROT_READ | PROT_WRITE | PROT_EXEC;
  area->flags = flags & (MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS);

  if (flags & MAP_ANONYMOUS) {
    area->offset = 0;
    if (kind == MAP_SHARED) {
      area->shared = shm_create(size);
      if (!area->shared) error = -ENOMEM;
    }
  } else {
    open_file_description_t *file = file_table_get(process->fd_table, fd);
    if (!file) error = -EBADF;
    else if (file->type == FILE_TYPE_SHM) {
      area->max_prot = PROT_READ;
      if ((file->flags & O_ACCMODE) == O_RDWR)
        area->max_prot |= PROT_WRITE;
      if ((prot & ~area->max_prot) ||
          (file->flags & O_ACCMODE) == O_WRONLY) {
        error = -EACCES;
      } else if ((uint64_t)offset > shm_size(file->shm_object) ||
          size > shm_size(file->shm_object) - (uint64_t)offset) {
        error = -EINVAL;
      } else {
        area->shared = file->shm_object;
        shm_retain(area->shared);
      }
    } else if (file->type != FILE_TYPE_FILE ||
               ((vfs_node_t *)file->vfs_node)->type != VFS_FILE) {
      error = -ENOTSUP;
    } else if (kind == MAP_SHARED) {
      /* Aucun page cache coherent/writeback n'existe encore dans le VFS. */
      error = -ENOTSUP;
    } else if ((file->flags & 3) == 1) {
      error = -EACCES;
    } else if ((uint64_t)offset > UINT32_MAX ||
               size > (uint64_t)UINT32_MAX + 1 - (uint64_t)offset) {
      error = -EINVAL;
    } else {
      area->file = file;
      file_description_retain(file);
    }
  }
  if (!error && fixed) error = split_range(process, address, end);
  if (error) {
    free_area(area);
    return error;
  }
  if (fixed) remove_range(process, address, end);
  vm_area_t **link = &process->vm_areas;
  while (*link && (*link)->start < address) link = &(*link)->next;
  area->next = *link;
  *link = area;
  merge_areas(process);
  return (int64_t)address;
}

int vm_munmap(process_t *process, uint64_t address, uint64_t length) {
  uint64_t end;
  if (!process || !range_valid(address, length, &end)) return -EINVAL;
  int error = split_range(process, address, end);
  if (error) return error;
  remove_range(process, address, end);
  merge_areas(process);
  return 0;
}

int vm_mprotect(process_t *process, uint64_t address, uint64_t length,
                 int prot) {
  uint64_t end;
  int error = protection_valid(prot);
  if (error) return error;
  if (!process || !range_valid(address, length, &end)) return -EINVAL;
  if (!range_covered(process, address, end)) return -ENOMEM;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (area->start >= end) break;
    if (area->end > address && (prot & ~area->max_prot)) return -EACCES;
  }
  error = split_range(process, address, end);
  if (error) return error;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (area->start >= address && area->end <= end) {
      area->prot = prot;
      vmm_update_range((page_directory_t *)process->pml4, area->start,
                        area->end, page_flags(prot), false);
    }
  }
  merge_areas(process);
  return 0;
}

int vm_madvise(process_t *process, uint64_t address, uint64_t length,
               int advice) {
  uint64_t end;
  if (!process || !range_valid(address, length, &end)) return -EINVAL;
  if (advice != MADV_DONTNEED) return -ENOTSUP;
  if (!range_covered(process, address, end)) return -ENOMEM;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    if (area->start >= end) break;
    if (area->end <= address) continue;
    if (area->flags & MAP_SHARED) return -ENOTSUP;
  }
  vmm_update_range((page_directory_t *)process->pml4, address, end, 0, true);
  return 0;
}

vm_fault_result_t vm_handle_fault(process_t *process, uint64_t address,
                                  uint64_t error_code) {
  if (!process || !process->pml4 || (error_code & (1 | 8)))
    return VM_FAULT_UNHANDLED;
  preempt_disable();
  vm_area_t *area = find_area(process, address);
  vm_fault_result_t result = VM_FAULT_UNHANDLED;
  if (!area || area->prot == PROT_NONE ||
      ((error_code & 2) && !(area->prot & PROT_WRITE)) ||
      ((error_code & 16) && !(area->prot & PROT_EXEC))) goto done;

  uint64_t page = PAGE_ALIGN_DOWN(address);
  page_directory_t *dir = (page_directory_t *)process->pml4;
  if (vmm_get_page_entry(dir, page) != 0) goto done;
  uint64_t backing_offset = area->offset + page - area->start;
  uint64_t flags = page_flags(area->prot);
  void *allocation = NULL;
  uint64_t phys;
  if (area->shared && (area->flags & MAP_SHARED)) {
    phys = shm_page_physical(area->shared, backing_offset / PAGE_SIZE);
    if (!phys) goto done;
  } else {
    allocation = pmm_alloc_block();
    if (!allocation) goto failure;
    memset(allocation, 0, PAGE_SIZE);
    if (area->shared) {
      uint64_t source =
          shm_page_physical(area->shared, backing_offset / PAGE_SIZE);
      if (!source) goto failure;
      memcpy(allocation, vmm_phys_to_virt(source), PAGE_SIZE);
    } else if (area->file) {
      vfs_node_t *node = area->file->vfs_node;
      result = VM_FAULT_BACKING_ERROR;
      if (backing_offset >= node->size) goto failure;
      uint64_t bytes = node->size - backing_offset;
      if (bytes > PAGE_SIZE) bytes = PAGE_SIZE;
      if (vfs_read(node, (uint32_t)backing_offset, (uint32_t)bytes,
                    allocation) != (int)bytes) goto failure;
      result = VM_FAULT_UNHANDLED;
    }
    phys = pmm_virt_to_phys(allocation);
    flags |= PAGE_OWNED;
  }
  if (vmm_map_page_in_dir(dir, phys, page, flags) != 0) goto failure;
  result = VM_FAULT_HANDLED;
  goto done;
failure:
  if (allocation) pmm_free_block(allocation);
  KLOG_ERROR("VM", "Unable to populate mmap page");
done:
  preempt_enable();
  return result;
}

int vm_clone_areas(const process_t *parent, process_t *child) {
  vm_area_t **link = &child->vm_areas;
  for (vm_area_t *area = parent->vm_areas; area; area = area->next) {
    vm_area_t *copy = kmalloc(sizeof(*copy));
    if (!copy) {
      vm_cleanup(child);
      return -ENOMEM;
    }
    *copy = *area;
    copy->next = NULL;
    retain_backing(copy);
    *link = copy;
    link = &copy->next;
  }
  return 0;
}

void vm_cleanup(process_t *process) {
  while (process->vm_areas) {
    vm_area_t *area = process->vm_areas;
    process->vm_areas = area->next;
    free_area(area);
  }
}

void vm_get_info(process_t *process, vm_info_t *info) {
  info->physical_total = pmm_get_total_blocks() * PAGE_SIZE;
  info->physical_free = pmm_get_free_blocks() * PAGE_SIZE;
  info->virtual_size = 0;
  info->resident_size = 0;
  for (vm_area_t *area = process->vm_areas; area; area = area->next) {
    info->virtual_size += area->end - area->start;
    info->resident_size +=
        vmm_resident_pages((page_directory_t *)process->pml4,
                           area->start, area->end) * PAGE_SIZE;
  }
}
