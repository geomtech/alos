/* src/userland/libc/src/stdlib/malloc.c - Userland Memory Allocation */
#include "../internal/syscall.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
} mem_block_t;

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
        current->next->free) {
      // Fusionner current et current->next
      current->size += MIN_BLOCK_SIZE + current->next->size;
      current->next = current->next->next;
    } else {
      current = current->next;
    }
  }
}

/* Allouer de la mémoire depuis le heap */
void *malloc(size_t size) {
  if (size == 0)
    return NULL;

  // Aligner la taille sur 8 octets
  size = (size + 7) & ~7;

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
void free(void *ptr) {
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
void *realloc(void *ptr, size_t size) {
  if (!ptr)
    return malloc(size);
  if (size == 0) {
    free(ptr);
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
  void *new_ptr = malloc(size);
  if (new_ptr) {
    memcpy(new_ptr, ptr, block->size);
    free(ptr);
  }
  return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
  size_t total_size = nmemb * size;
  void *ptr = malloc(total_size);
  if (ptr) {
    memset(ptr, 0, total_size);
  }
  return ptr;
}
