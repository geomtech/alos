/* src/kernel/shared_memory.h - Objets de memoire partagee userland */
#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stddef.h>
#include <stdint.h>

struct process;
struct open_file_description;

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
uint64_t shm_page_physical(const shm_object_t *object, uint64_t page);
int shm_create_descriptor(struct process *process, uint64_t size);
int shm_readonly_descriptor(struct process *process, int fd);
int64_t shm_descriptor_info(struct process *process, int fd, int query);
int shm_descriptors_same(struct process *process, int fd, int other_fd);

void *shm_map_process(struct process *process, shm_object_t *object);
int shm_unmap_process(struct process *process, void *address);
int shm_clone_process_mappings(const struct process *parent,
                               struct process *child);
void shm_cleanup_process(struct process *process);

#endif
