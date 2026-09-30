/* src/userland/libc/src/stdlib/malloc.c - Userland Memory Allocation */
#include "../internal/syscall.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/futex.h>
#include <stdio.h>
#include <unistd.h>

/* Syscall pour brk - obtenir/étendre le heap */
static void *sys_brk(void *addr) {
  /* Use correct syscall SYS_BRK (120) */
  return (void *)syscall3(120, (long)addr, 0, 0);
}

/* Structure pour un bloc mémoire */
typedef struct mem_block {
  size_t size;            /* Taille du bloc (sans l'en-tête) */
  struct mem_block *next; /* Pointeur vers le bloc suivant */
  int free;               /* 1 si libre, 0 si alloué */
} __attribute__((aligned(16))) mem_block_t;

static uint32_t allocator_lock;
static void lock_heap(void) {
  while (__atomic_exchange_n(&allocator_lock, 1, __ATOMIC_ACQUIRE)) {
    if (futex_wait(&allocator_lock, 1, 0) && errno != EAGAIN) {
      puts("malloc: heap wait failed");
      _exit(134);
    }
  }
}
static void unlock_heap(void) {
  __atomic_store_n(&allocator_lock, 0, __ATOMIC_RELEASE);
  futex_wake(&allocator_lock, 1);
}

/* Taille minimale d'un bloc */
#define MIN_BLOCK_SIZE sizeof(mem_block_t)

/* Heap initial - sera étendu avec brk() */
static void *heap_start = NULL;
static void *heap_end = NULL;
static mem_block_t *free_list = NULL;

/* Initialiser le heap */
static void init_heap() {
  if (heap_start)
    return;

  // Obtenir l'adresse actuelle de brk
  void *current_brk = sys_brk(NULL);

  // Allouer au moins une page (4096 octets)
  // Si current_brk est déjà aligné, on ajoute une page complète
  uintptr_t aligned_brk = ((uintptr_t)current_brk + 4095) & ~4095;
  if (aligned_brk == (uintptr_t)current_brk) {
    // Déjà aligné, ajouter une page
    aligned_brk += 4096;
  }
  void *new_brk = (void *)aligned_brk;

  if (sys_brk(new_brk) != new_brk) {
    // Échec de l'allocation initiale
    return;
  }

  heap_start = current_brk;
  heap_end = new_brk;

  // Créer le premier bloc libre
  mem_block_t *first_block = (mem_block_t *)heap_start;
  first_block->size = (size_t)(heap_end - heap_start) - MIN_BLOCK_SIZE;
  first_block->next = NULL;
  first_block->free = 1;
  free_list = first_block;
}

/* Diviser un bloc en deux */
static void split_block(mem_block_t *block, size_t size) {
  if (block->size < size + MIN_BLOCK_SIZE) {
    return; // Pas assez d'espace pour diviser
  }

  // Calculer l'adresse du nouveau bloc
  mem_block_t *new_block =
      (mem_block_t *)((uint8_t *)block + MIN_BLOCK_SIZE + size);

  // Mettre à jour le bloc actuel
  new_block->size = block->size - size - MIN_BLOCK_SIZE;
  new_block->next = block->next;
  new_block->free = 1;

  // Mettre à jour le bloc original
  block->size = size;
  block->next = new_block;
}

/* Fusionner les blocs libres adjacents */
static void merge_blocks() {
  mem_block_t *current = free_list;
  while (current && current->next) {
    if ((uint8_t *)current + MIN_BLOCK_SIZE + current->size ==
            (uint8_t *)current->next &&
        current->free && current->next->free) {
      // Fusionner current et current->next
      current->size += MIN_BLOCK_SIZE + current->next->size;
      current->next = current->next->next;
    } else {
      current = current->next;
    }
  }
}

/* Allouer de la mémoire depuis le heap */
static void *malloc_locked(size_t size) {
  if (size == 0)
    return NULL;

  if (size > (size_t)-1 - 15 - MIN_BLOCK_SIZE) return NULL;
  size = (size + 15) & ~(size_t)15;

  if (!heap_start) {
    init_heap();
    if (!heap_start)
      return NULL;
  }

  // Rechercher un bloc libre suffisamment grand
  mem_block_t *prev = NULL;
  mem_block_t *current = free_list;

  while (current) {
    if (current->free && current->size >= size) {
      // Bloc trouvé, le diviser si nécessaire
      if (current->size > size + MIN_BLOCK_SIZE) {
        split_block(current, size);
      }

      current->free = 0;

      // Retourner le pointeur vers la zone de données (après l'en-tête)
      return (void *)((uint8_t *)current + MIN_BLOCK_SIZE);
    }

    prev = current;
    current = current->next;
  }

  // Aucun bloc libre trouvé, étendre le heap
  size_t needed_size = size + MIN_BLOCK_SIZE;
  if ((uintptr_t)heap_end > (uintptr_t)-1 - needed_size) return NULL;
  void *new_brk = (void *)((uintptr_t)heap_end + needed_size);

  if (sys_brk(new_brk) != new_brk) {
    return NULL; // Échec de l'extension du heap
  }

  // Créer un nouveau bloc
  mem_block_t *new_block = (mem_block_t *)heap_end;
  new_block->size = size;
  new_block->next = NULL;
  new_block->free = 0;

  // Ajouter à la liste libre (même s'il est alloué, pour la gestion)
  if (prev) {
    prev->next = new_block;
  } else {
    free_list = new_block;
  }

  heap_end = new_brk;

  // Retourner le pointeur vers la zone de données
  return (void *)((uint8_t *)new_block + MIN_BLOCK_SIZE);
}

/* Libérer de la mémoire */
static void free_locked(void *ptr) {
  if (!ptr)
    return;

  // Obtenir l'en-tête du bloc
  mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - MIN_BLOCK_SIZE);

  // Vérifier que le pointeur est valide
  if ((uint8_t *)block < (uint8_t *)heap_start ||
      (uint8_t *)block >= (uint8_t *)heap_end) {
    return; // Pointeur invalide
  }

  block->free = 1;

  // Fusionner les blocs adjacents
  merge_blocks();
}

/* Réallouer de la mémoire */
static void *realloc_locked(void *ptr, size_t size) {
  if (!ptr)
    return malloc_locked(size);
  if (size == 0) {
    free_locked(ptr);
    return NULL;
  }

  // Obtenir l'en-tête du bloc
  mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - MIN_BLOCK_SIZE);

  // Vérifier que le pointeur est valide
  if ((uint8_t *)block < (uint8_t *)heap_start ||
      (uint8_t *)block >= (uint8_t *)heap_end) {
    return NULL; // Pointeur invalide
  }

  // Si le bloc est déjà suffisamment grand
  if (block->size >= size) {
    // Diviser si nécessaire
    if (block->size > size + MIN_BLOCK_SIZE) {
      split_block(block, size);
    }
    return ptr;
  }

  // Sinon, allouer un nouveau bloc et copier
  void *new_ptr = malloc_locked(size);
  if (new_ptr) {
    memcpy(new_ptr, ptr, block->size);
    free_locked(ptr);
  }
  return new_ptr;
}

void *malloc(size_t size) {
  lock_heap();
  void *result = malloc_locked(size);
  unlock_heap();
  if (!result && size) errno = ENOMEM;
  return result;
}

static void *allocate_aligned(size_t alignment, size_t size) {
  if (alignment <= 16) return malloc(size);
  if (size > (size_t)-1 - alignment ||
      size + alignment > (size_t)-1 - 2 * MIN_BLOCK_SIZE - 16) {
    errno = ENOMEM;
    return NULL;
  }
  lock_heap();
  void *base = malloc_locked(size + alignment + 2 * MIN_BLOCK_SIZE + 16);
  void *result = NULL;
  if (base) {
    mem_block_t *prefix = (mem_block_t *)((uint8_t *)base - MIN_BLOCK_SIZE);
    uintptr_t data = ((uintptr_t)base + MIN_BLOCK_SIZE + 16 + alignment - 1) &
                      ~(uintptr_t)(alignment - 1);
    mem_block_t *block = (mem_block_t *)(data - MIN_BLOCK_SIZE);
    size_t padding = (size_t)((uint8_t *)block - (uint8_t *)prefix) - MIN_BLOCK_SIZE;
    block->size = prefix->size - padding - MIN_BLOCK_SIZE;
    block->next = prefix->next;
    block->free = 0;
    prefix->size = padding;
    prefix->next = block;
    prefix->free = 1;
    size_t aligned_size = (size + 15) & ~(size_t)15;
    if (block->size > aligned_size + MIN_BLOCK_SIZE)
      split_block(block, aligned_size);
    result = (void *)data;
  }
  unlock_heap();
  if (!result && size) errno = ENOMEM;
  return result;
}

void *aligned_alloc(size_t alignment, size_t size) {
  if (!alignment || (alignment & (alignment - 1)) || size % alignment) {
    errno = EINVAL;
    return NULL;
  }
  return allocate_aligned(alignment, size);
}

int posix_memalign(void **result, size_t alignment, size_t size) {
  if (!result || alignment < sizeof(void *) ||
      (alignment & (alignment - 1))) return EINVAL;
  int saved_errno = errno;
  void *allocation = allocate_aligned(alignment, size);
  int error = allocation || !size ? 0 : ENOMEM;
  errno = saved_errno;
  if (!error) *result = allocation;
  return error;
}

void free(void *ptr) {
  lock_heap();
  free_locked(ptr);
  unlock_heap();
}

void *realloc(void *ptr, size_t size) {
  if (size > (size_t)-1 - 15 - MIN_BLOCK_SIZE) {
    errno = ENOMEM;
    return NULL;
  }
  size_t aligned = (size + 15) & ~(size_t)15;
  lock_heap();
  void *result = realloc_locked(ptr, aligned);
  unlock_heap();
  if (!result && size) errno = ENOMEM;
  return result;
}

void *calloc(size_t nmemb, size_t size) {
  if (size && nmemb > (size_t)-1 / size) {
    errno = ENOMEM;
    return NULL;
  }
  size_t total_size = nmemb * size;
  void *ptr = malloc(total_size);
  if (ptr) {
    memset(ptr, 0, total_size);
  }
  return ptr;
}
