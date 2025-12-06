/* src/kheap.h - Kernel Heap Allocator */
#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Structure d'en-tête pour chaque bloc du heap.
 * Placée juste avant les données utilisateur.
 */
typedef struct KHeapBlock {
    size_t size;                /* Taille des données (sans le header) */
    bool is_free;               /* true si le bloc est libre */
    struct KHeapBlock* next;    /* Pointeur vers le bloc suivant */
} KHeapBlock;

/* Taille minimale d'un bloc de données (pour éviter des blocs trop petits) */
#define KHEAP_MIN_BLOCK_SIZE    16

/* Macro pour obtenir l'adresse des données depuis un header */
#define KHEAP_BLOCK_DATA(block) ((void*)((uint8_t*)(block) + sizeof(KHeapBlock)))

/* Macro pour obtenir le header depuis l'adresse des données */
#define KHEAP_DATA_BLOCK(ptr)   ((KHeapBlock*)((uint8_t*)(ptr) - sizeof(KHeapBlock)))

/**
 * Initialise le kernel heap avec une zone mémoire donnée.
 * 
 * @param start_addr Adresse de début de la zone mémoire pour le heap
 * @param size_bytes Taille totale de la zone en octets
 */
void kheap_init(void* start_addr, size_t size_bytes);

/**
 * Alloue un bloc de mémoire de la taille demandée.
 * Utilise l'algorithme First Fit.
 * 
 * @param size Taille en octets à allouer
 * @return Pointeur vers la zone allouée, ou NULL si échec
 */
void* kmalloc(size_t size);

/**
 * Libère un bloc de mémoire précédemment alloué.
 * Tente de fusionner avec les blocs adjacents libres.
 * 
 * @param ptr Pointeur vers la zone à libérer (retourné par kmalloc)
 */
void kfree(void* ptr);

/**
 * Réalloue un bloc de mémoire avec une nouvelle taille.
 * - Si ptr est NULL, équivalent à kmalloc(new_size)
 * - Si new_size est 0, équivalent à kfree(ptr) et retourne NULL
 * - Sinon, alloue un nouveau bloc, copie les données, libère l'ancien
 * 
 * @param ptr Pointeur vers la zone à réallouer (ou NULL)
 * @param new_size Nouvelle taille en octets
 * @return Pointeur vers la nouvelle zone, ou NULL si échec
 */
void* krealloc(void* ptr, size_t new_size);

/**
 * Retourne la taille totale du heap en octets.
 */
size_t kheap_get_total_size(void);

/**
 * Retourne la quantité de mémoire libre dans le heap.
 */
size_t kheap_get_free_size(void);

/**
 * Retourne le nombre de blocs dans le heap.
 */
size_t kheap_get_block_count(void);

/**
 * Retourne le nombre de blocs libres.
 */
size_t kheap_get_free_block_count(void);

#endif /* KHEAP_H */
