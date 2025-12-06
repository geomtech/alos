/* src/include/memlayout.h - Memory Layout Constants for x86-64
 *
 * Ce fichier centralise toutes les constantes de disposition mémoire
 * pour éviter les conflits entre les différents modules du noyau.
 *
 * IMPORTANT: Toute modification de ces valeurs doit être faite ici uniquement.
 */
#ifndef MEMLAYOUT_H
#define MEMLAYOUT_H

#include <stdint.h>

/* ========================================
 * Disposition de l'espace virtuel x86-64
 * ========================================
 *
 * L'espace d'adressage canonique x86-64 (48 bits) :
 *
 * 0x0000000000000000 - 0x00007FFFFFFFFFFF : User space (128 TB)
 * 0xFFFF800000000000 - 0xFFFF87FFFFFFFFFF : HHDM (Limine) - 8 TB
 * 0xFFFF900000000000 - 0xFFFF9FFFFFFFFFFF : MMIO Zone - 16 TB (PML4 #274-275)
 * 0xFFFFFFFF80000000 - 0xFFFFFFFFFFFFFFFF : Kernel code (mcmodel=kernel)
 *
 * Index PML4 pour référence:
 *   #256 = 0xFFFF800000000000 (HHDM start)
 *   #274 = 0xFFFF900000000000 (MMIO zone - SAFE)
 *   #510 = 0xFFFFFF0000000000 (Recursive mapping - DANGER)
 *   #511 = 0xFFFFFFFF80000000 (Kernel code - DANGER)
 */

/* ========================================
 * HHDM (Higher Half Direct Map)
 * ======================================== */

/* Début de la zone HHDM (géré par Limine)
 * Note: La valeur exacte est fournie par Limine au boot.
 * Cette constante est utilisée comme valeur par défaut/référence. */
#define HHDM_OFFSET_DEFAULT     0xFFFF800000000000ULL

/* ========================================
 * Zone MMIO (Memory-Mapped I/O)
 * ======================================== */

/* Zone dédiée aux mappings MMIO des périphériques.
 * 
 * Choix de 0xFFFF900000000000 (PML4 index #274):
 * - Loin du HHDM (index #256-271)
 * - Loin du kernel code (index #511)
 * - Loin du recursive mapping potentiel (index #510)
 * - 16 TB d'espace disponible (largement suffisant)
 */
#define MMIO_VIRT_BASE          0xFFFF900000000000ULL
#define MMIO_VIRT_END           0xFFFFA00000000000ULL  /* 16 TB */
#define MMIO_VIRT_SIZE          (MMIO_VIRT_END - MMIO_VIRT_BASE)

/* ========================================
 * Zone Kernel
 * ======================================== */

/* Début de la zone kernel (où le code est chargé par Limine)
 * Correspond au mcmodel=kernel de GCC (-2GB) */
#define KERNEL_VIRT_BASE        0xFFFFFFFF80000000ULL

/* ========================================
 * Zone User Space
 * ======================================== */

/* Limites de l'espace utilisateur */
#define USER_SPACE_START        0x0000000000000000ULL
#define USER_SPACE_END          0x00007FFFFFFFFFFFULL

/* Adresse de base pour le code utilisateur */
#define USER_CODE_BASE          0x0000000000400000ULL

/* Adresse de base pour la stack utilisateur */
#define USER_STACK_TOP          0x00007FFFFFFFE000ULL
#define USER_STACK_SIZE         (16 * 4096)  /* 64 KB */

/* ========================================
 * Helpers
 * ======================================== */

/* Vérifie si une adresse est dans la zone MMIO */
static inline int is_mmio_address(uint64_t virt)
{
    return (virt >= MMIO_VIRT_BASE && virt < MMIO_VIRT_END);
}

/* Vérifie si une adresse est dans l'espace kernel */
static inline int is_kernel_address(uint64_t virt)
{
    return (virt >= KERNEL_VIRT_BASE);
}

/* Vérifie si une adresse est dans l'espace user */
static inline int is_user_address(uint64_t virt)
{
    return (virt <= USER_SPACE_END);
}

#endif /* MEMLAYOUT_H */
