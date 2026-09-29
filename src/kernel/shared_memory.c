/* src/kernel/shared_memory.c - Objets de memoire partagee userland */
#include "shared_memory.h"
#include "../include/string.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "process.h"

struct shm_object {
  volatile int ref_count;
  uint64_t size;
  uint32_t page_count;
  void **pages;
};

shm_object_t *shm_create(size_t size) {
  if (size == 0 || size > SHM_MAX_SIZE) {
    return NULL;
  }

  uint64_t aligned_size = PAGE_ALIGN_UP((uint64_t)size);
  uint32_t page_count = (uint32_t)(aligned_size / PAGE_SIZE);
  shm_object_t *object = (shm_object_t *)kmalloc(sizeof(*object));
  if (object == NULL) {
    return NULL;
  }
  memset(object, 0, sizeof(*object));

  object->pages = (void **)kmalloc(sizeof(void *) * page_count);
  if (object->pages == NULL) {
    kfree(object);
    return NULL;
  }
  memset(object->pages, 0, sizeof(void *) * page_count);
  object->ref_count = 1;
  object->size = aligned_size;
  object->page_count = page_count;

  for (uint32_t i = 0; i < page_count; i++) {
    object->pages[i] = pmm_alloc_block();
    if (object->pages[i] == NULL) {
      shm_release(object);
      return NULL;
    }
    memset(object->pages[i], 0, PAGE_SIZE);
  }
  return object;
}

void shm_retain(shm_object_t *object) {
  if (object != NULL) {
    __atomic_add_fetch(&object->ref_count, 1, __ATOMIC_SEQ_CST);
  }
}

void shm_release(shm_object_t *object) {
  if (object == NULL) {
    return;
  }
  if (__atomic_sub_fetch(&object->ref_count, 1, __ATOMIC_SEQ_CST) > 0) {
    return;
  }

  for (uint32_t i = 0; i < object->page_count; i++) {
    if (object->pages[i] != NULL) {
      pmm_free_block(object->pages[i]);
    }
  }
  kfree(object->pages);
  kfree(object);
}

size_t shm_size(const shm_object_t *object) {
  return object != NULL ? (size_t)object->size : 0;
}

static bool range_is_free(process_t *process, uint64_t address, uint64_t size) {
  page_directory_t *directory = (page_directory_t *)process->pml4;
  for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
    if (vmm_is_mapped_in_dir(directory, address + offset)) {
      return false;
    }
  }
  return true;
}

void *shm_map_process(process_t *process, shm_object_t *object) {
  if (process == NULL || process->pml4 == NULL || object == NULL) {
    return (void *)-1;
  }

  int mapping_index = -1;
  for (int i = 0; i < SHM_MAX_MAPPINGS; i++) {
    if (process->shm_mappings[i].object == NULL) {
      mapping_index = i;
      break;
    }
  }
  if (mapping_index < 0) {
    return (void *)-1;
  }

  uint64_t address = USER_SHM_BASE;
  while (address + object->size <= USER_SHM_END &&
         !range_is_free(process, address, object->size)) {
    address += PAGE_SIZE;
  }
  if (address + object->size > USER_SHM_END) {
    return (void *)-1;
  }

  page_directory_t *directory = (page_directory_t *)process->pml4;
  uint32_t mapped = 0;
  for (; mapped < object->page_count; mapped++) {
    uint64_t physical = pmm_virt_to_phys(object->pages[mapped]);
    if (vmm_map_page_in_dir(directory, physical,
                            address + (uint64_t)mapped * PAGE_SIZE,
                            PAGE_PRESENT | PAGE_USER | PAGE_RW | PAGE_NX) != 0) {
      break;
    }
  }
  if (mapped != object->page_count) {
    while (mapped > 0) {
      mapped--;
      vmm_unmap_page_in_dir(directory,
                            address + (uint64_t)mapped * PAGE_SIZE);
    }
    return (void *)-1;
  }

  shm_retain(object);
  process->shm_mappings[mapping_index].object = object;
  process->shm_mappings[mapping_index].address = address;
  process->shm_mappings[mapping_index].size = object->size;
  return (void *)address;
}

int shm_unmap_process(process_t *process, void *address_pointer) {
  if (process == NULL || process->pml4 == NULL) {
    return -1;
  }
  uint64_t address = (uint64_t)address_pointer;
  for (int i = 0; i < SHM_MAX_MAPPINGS; i++) {
    shm_process_mapping_t *mapping = &process->shm_mappings[i];
    if (mapping->object == NULL || mapping->address != address) {
      continue;
    }

    page_directory_t *directory = (page_directory_t *)process->pml4;
    for (uint64_t offset = 0; offset < mapping->size; offset += PAGE_SIZE) {
      vmm_unmap_page_in_dir(directory, mapping->address + offset);
    }
    shm_release(mapping->object);
    memset(mapping, 0, sizeof(*mapping));
    return 0;
  }
  return -1;
}

int shm_clone_process_mappings(const process_t *parent, process_t *child) {
  if (parent == NULL || child == NULL) {
    return -1;
  }
  for (int i = 0; i < SHM_MAX_MAPPINGS; i++) {
    if (parent->shm_mappings[i].object == NULL) {
      continue;
    }
    child->shm_mappings[i] = parent->shm_mappings[i];
    shm_retain(child->shm_mappings[i].object);
  }
  return 0;
}

void shm_cleanup_process(process_t *process) {
  if (process == NULL) {
    return;
  }
  for (int i = 0; i < SHM_MAX_MAPPINGS; i++) {
    shm_process_mapping_t *mapping = &process->shm_mappings[i];
    if (mapping->object != NULL) {
      shm_release(mapping->object);
      memset(mapping, 0, sizeof(*mapping));
    }
  }
}
