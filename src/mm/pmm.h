/* src/mm/pmm.h - Physical Memory Manager for x86-64 */
#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "../include/limine.h"

/* Taille d'un bloc/page physique : 4 KiB */
#define PMM_BLOCK_SIZE      4096ULL
#define PMM_BLOCKS_PER_BYTE 8

/* Aligne une adresse vers le haut au prochain bloc */
#define PMM_ALIGN_UP(addr)   (((addr) + PMM_BLOCK_SIZE - 1) & ~(PMM_BLOCK_SIZE - 1))
/* Aligne une adresse vers le bas au bloc précédent */
#define PMM_ALIGN_DOWN(addr) ((addr) & ~(PMM_BLOCK_SIZE - 1))

/* Convertit une adresse en index de bloc */
#define PMM_ADDR_TO_BLOCK(addr) ((addr) / PMM_BLOCK_SIZE)
/* Convertit un index de bloc en adresse */
#define PMM_BLOCK_TO_ADDR(block) ((uint64_t)(block) * PMM_BLOCK_SIZE)

/**
 * Initialise le PMM en parsant la memory map Limine.
 * Marque automatiquement les zones réservées et le kernel comme occupées.
 * 
 * @param memmap Pointeur vers la réponse memmap de Limine
 * @param hhdm_offset Offset HHDM pour conversion phys->virt
 */
void init_pmm_limine(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/**
 * Alloue un bloc de mémoire physique de 4 KiB.
 * 
 * @return Adresse physique du bloc alloué, ou NULL si plus de mémoire disponible
 */
void* pmm_alloc_block(void);

/**
 * Alloue plusieurs blocs contigus de mémoire physique.
 * 
 * @param count Nombre de blocs de 4 KiB à allouer
 * @return Adresse physique du premier bloc, ou NULL si pas assez de mémoire contiguë
 */
void* pmm_alloc_blocks(uint64_t count);

/**
 * Libère un bloc de mémoire physique précédemment alloué.
 * 
 * @param p Adresse physique du bloc à libérer (doit être alignée sur 4 KiB)
 */
void pmm_free_block(void* p);

/**
 * Libère plusieurs blocs contigus de mémoire physique.
 * 
 * @param p Adresse physique du premier bloc
 * @param count Nombre de blocs à libérer
 */
void pmm_free_blocks(void* p, uint64_t count);

/**
 * Retourne le nombre total de blocs gérés par le PMM.
 */
uint64_t pmm_get_total_blocks(void);

/**
 * Retourne le nombre de blocs actuellement utilisés.
 */
uint64_t pmm_get_used_blocks(void);

/**
 * Retourne le nombre de blocs libres disponibles.
 */
uint64_t pmm_get_free_blocks(void);

/**
 * Retourne la quantité de mémoire libre en octets.
 */
uint64_t pmm_get_free_memory(void);

/**
 * Convertit une adresse physique en adresse virtuelle (via HHDM).
 */
void* pmm_phys_to_virt(uint64_t phys);

/**
 * Convertit une adresse virtuelle en adresse physique.
 */
uint64_t pmm_virt_to_phys(void* virt);

#endif /* PMM_H */
