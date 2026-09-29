/* src/kernel/shared_memory.h - Objets de memoire partagee userland */
#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stddef.h>
#include <stdint.h>

struct process;

typedef struct shm_object shm_object_t;

#define SHM_MAX_SIZE (16U * 1024U * 1024U)
#define SHM_MAX_MAPPINGS 32

typedef struct {
  shm_object_t *object;
  uint64_t address;
  uint64_t size;
} shm_process_mapping_t;

shm_object_t *shm_create(size_t size);
void shm_retain(shm_object_t *object);
void shm_release(shm_object_t *object);
size_t shm_size(const shm_object_t *object);

void *shm_map_process(struct process *process, shm_object_t *object);
int shm_unmap_process(struct process *process, void *address);
int shm_clone_process_mappings(const struct process *parent,
                               struct process *child);
void shm_cleanup_process(struct process *process);

#endif
