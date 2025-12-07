/* src/lib/umalloc.h - Userland Memory Allocation Header */
#ifndef USERLAND_MALLOC_H
#define USERLAND_MALLOC_H

#include <stdint.h>
#include <stddef.h>

/**
 * Alloue de la mémoire dans le userland
 * @param size Taille en octets à allouer
 * @return Pointeur vers la mémoire allouée, ou NULL en cas d'échec
 */
void* umalloc(size_t size);

/**
 * Libère de la mémoire allouée précédemment
 * @param ptr Pointeur à libérer (peut être NULL)
 */
void ufree(void* ptr);

/**
 * Réalloue un bloc de mémoire
 * @param ptr Pointeur existant (peut être NULL)
 * @param size Nouvelle taille
 * @return Pointeur vers la nouvelle mémoire, ou NULL en cas d'échec
 */
void* urealloc(void* ptr, size_t size);

/**
 * Obtient la taille utilisable d'un bloc alloué
 * @param ptr Pointeur alloué
 * @return Taille en octets, ou 0 si le pointeur est invalide
 */
size_t umalloc_usable_size(void* ptr);

#endif /* USERLAND_MALLOC_H */