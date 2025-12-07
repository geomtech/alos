/* src/lib/umalloc.c - Userland Memory Allocation */
#include "../include/string.h"
#include <stdint.h>

/* Syscall wrapper pour x86-64 */
static inline long syscall3(long num, long arg1, long arg2, long arg3)
{
    long result;
    __asm__ volatile (
        "int $0x80"
        : "=a" (result)
        : "a" (num), "D" (arg1), "S" (arg2), "d" (arg3)
        : "memory", "rcx", "r11"
    );
    return result;
}

/* Syscall pour brk - obtenir/étendre le heap */
static void* sys_brk(void* addr)
{
    // Utiliser le syscall SYS_BRK si disponible, sinon utiliser mmap
    // Pour l'instant, implémentation simplifiée
    return (void*)syscall3(45, (long)addr, 0, 0); // SYS_BRK = 45
}

/* Structure pour un bloc mémoire */
typedef struct mem_block {
    size_t size;             /* Taille du bloc (sans l'en-tête) */
    struct mem_block* next;  /* Pointeur vers le bloc suivant */
    int free;                /* 1 si libre, 0 si alloué */
} mem_block_t;

/* Taille minimale d'un bloc */
#define MIN_BLOCK_SIZE sizeof(mem_block_t)

/* Heap initial - sera étendu avec brk() */
static void* heap_start = NULL;
static void* heap_end = NULL;
static mem_block_t* free_list = NULL;

/* Initialiser le heap */
static void init_heap()
{
    if (heap_start) return;

    // Obtenir l'adresse actuelle de brk
    void* current_brk = sys_brk(NULL);

    // Allouer une page initiale (4096 octets)
    void* new_brk = (void*)(((uintptr_t)current_brk + 4095) & ~4095);
    if (sys_brk(new_brk) != new_brk) {
        // Échec de l'allocation initiale
        return;
    }

    heap_start = current_brk;
    heap_end = new_brk;

    // Créer le premier bloc libre
    mem_block_t* first_block = (mem_block_t*)heap_start;
    first_block->size = (size_t)(heap_end - heap_start) - MIN_BLOCK_SIZE;
    first_block->next = NULL;
    first_block->free = 1;
    free_list = first_block;
}

/* Diviser un bloc en deux */
static void split_block(mem_block_t* block, size_t size)
{
    if (block->size < size + MIN_BLOCK_SIZE) {
        return; // Pas assez d'espace pour diviser
    }

    // Calculer l'adresse du nouveau bloc
    mem_block_t* new_block = (mem_block_t*)((uint8_t*)block + MIN_BLOCK_SIZE + size);

    // Mettre à jour le bloc actuel
    new_block->size = block->size - size - MIN_BLOCK_SIZE;
    new_block->next = block->next;
    new_block->free = 1;

    // Mettre à jour le bloc original
    block->size = size;
    block->next = new_block;
}

/* Fusionner les blocs libres adjacents */
static void merge_blocks()
{
    mem_block_t* current = free_list;
    while (current && current->next) {
        if ((uint8_t*)current + MIN_BLOCK_SIZE + current->size == (uint8_t*)current->next &&
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
void* umalloc(size_t size)
{
    if (size == 0) return NULL;

    // Aligner la taille sur 8 octets
    size = (size + 7) & ~7;

    if (!heap_start) {
        init_heap();
        if (!heap_start) return NULL;
    }

    // Rechercher un bloc libre suffisamment grand
    mem_block_t* prev = NULL;
    mem_block_t* current = free_list;

    while (current) {
        if (current->free && current->size >= size) {
            // Bloc trouvé, le diviser si nécessaire
            if (current->size > size + MIN_BLOCK_SIZE) {
                split_block(current, size);
            }

            current->free = 0;

            // Retourner le pointeur vers la zone de données (après l'en-tête)
            return (void*)((uint8_t*)current + MIN_BLOCK_SIZE);
        }

        prev = current;
        current = current->next;
    }

    // Aucun bloc libre trouvé, étendre le heap
    size_t needed_size = size + MIN_BLOCK_SIZE;
    void* new_brk = (void*)((uintptr_t)heap_end + needed_size);

    if (sys_brk(new_brk) != new_brk) {
        return NULL; // Échec de l'extension du heap
    }

    // Créer un nouveau bloc
    mem_block_t* new_block = (mem_block_t*)heap_end;
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
    return (void*)((uint8_t*)new_block + MIN_BLOCK_SIZE);
}

/* Libérer de la mémoire */
void ufree(void* ptr)
{
    if (!ptr) return;

    // Obtenir l'en-tête du bloc
    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - MIN_BLOCK_SIZE);

    // Vérifier que le pointeur est valide
    if ((uint8_t*)block < (uint8_t*)heap_start ||
        (uint8_t*)block >= (uint8_t*)heap_end) {
        return; // Pointeur invalide
    }

    block->free = 1;

    // Fusionner les blocs adjacents
    merge_blocks();
}

/* Réallouer de la mémoire */
void* urealloc(void* ptr, size_t size)
{
    if (!ptr) return umalloc(size);
    if (size == 0) {
        ufree(ptr);
        return NULL;
    }

    // Obtenir l'en-tête du bloc
    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - MIN_BLOCK_SIZE);

    // Vérifier que le pointeur est valide
    if ((uint8_t*)block < (uint8_t*)heap_start ||
        (uint8_t*)block >= (uint8_t*)heap_end) {
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
    void* new_ptr = umalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        ufree(ptr);
    }
    return new_ptr;
}

/* Obtenir la taille allouée pour un pointeur */
size_t umalloc_usable_size(void* ptr)
{
    if (!ptr) return 0;

    mem_block_t* block = (mem_block_t*)((uint8_t*)ptr - MIN_BLOCK_SIZE);

    if ((uint8_t*)block < (uint8_t*)heap_start ||
        (uint8_t*)block >= (uint8_t*)heap_end) {
        return 0;
    }

    return block->size;
}