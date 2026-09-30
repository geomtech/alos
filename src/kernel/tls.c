/* TLS ELF statique AMD64 Variant II : image avant le TCB pointe par FS. */
#include "tls.h"
#include "process.h"
#include "../mm/vm.h"
#include "../mm/vmm.h"
#include "../include/mman.h"
#include "../include/string.h"

int tls_setup_thread(process_t *process, thread_t *thread) {
  uint64_t alignment = process->tls_alignment;
  if (!alignment) alignment = 1;
  uint64_t size = (process->tls_mem_size + alignment - 1) & ~(alignment - 1);
  uint64_t tcb_alignment = alignment < 16 ? 16 : alignment;
  uint64_t mapping_size = PAGE_ALIGN_UP(size + tcb_alignment + 64);
  int64_t mapping = vm_mmap(process, 0, mapping_size,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping < 0) return -1;
  uint64_t tcb = ((uint64_t)mapping + size + tcb_alignment - 1) &
                 ~(tcb_alignment - 1);
  uint64_t destination = tcb - size;
  page_directory_t *dir = (page_directory_t *)process->pml4;
  for (uint64_t page = (uint64_t)mapping; page < (uint64_t)mapping + mapping_size;
       page += PAGE_SIZE) {
    if (vm_handle_fault(process, page, 2) != VM_FAULT_HANDLED) goto fail;
  }
  for (uint64_t offset = 0; offset < process->tls_file_size;) {
    uint64_t source = process->tls_image_address + offset;
    uint64_t phys = vmm_get_phys_addr(dir, source);
    if (!phys) goto fail;
    uint64_t chunk = PAGE_SIZE - (source & (PAGE_SIZE - 1));
    if (chunk > process->tls_file_size - offset)
      chunk = process->tls_file_size - offset;
    if (vmm_copy_to_dir(dir, destination + offset,
                         vmm_phys_to_virt(phys), chunk)) goto fail;
    offset += chunk;
  }
  uint64_t header[8] = {tcb, 0, tcb, thread->tid, 0, 0, 0, 0};
  if (vmm_copy_to_dir(dir, tcb, header, sizeof(header))) goto fail;
  thread->fs_base = tcb;
  thread->tls_mapping = (uint64_t)mapping;
  thread->tls_mapping_size = mapping_size;
  return 0;
fail:
  vm_munmap(process, (uint64_t)mapping, mapping_size);
  return -1;
}
