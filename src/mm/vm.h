/* src/mm/vm.h - Regions virtuelles par processus */
#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../include/vminfo.h"

struct process;
struct vm_area;

int64_t vm_mmap(struct process *process, uint64_t address, uint64_t length,
                 int prot, int flags, int fd, int64_t offset);
int vm_munmap(struct process *process, uint64_t address, uint64_t length);
int vm_mprotect(struct process *process, uint64_t address, uint64_t length,
                 int prot);
int vm_madvise(struct process *process, uint64_t address, uint64_t length,
               int advice);
int vm_msync(struct process *process, uint64_t address, uint64_t length, int flags);
int vm_mincore(struct process *process, uint64_t address, uint64_t length,
                unsigned char *vector);
typedef enum {
  VM_FAULT_UNHANDLED,
  VM_FAULT_HANDLED,
  VM_FAULT_BACKING_ERROR
} vm_fault_result_t;
vm_fault_result_t vm_handle_fault(struct process *process, uint64_t address,
                                  uint64_t error_code);
int vm_clone_areas(const struct process *parent, struct process *child);
void vm_cleanup(struct process *process);
void vm_get_info(struct process *process, vm_info_t *info);

#endif
