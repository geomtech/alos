/* src/kernel/mmio/mmio.h - Abstraction MMIO (Memory-Mapped I/O)
 *
 * Ce module fournit une couche d'abstraction pour les accès MMIO,
 * permettant de remplacer les instructions PIO (IN/OUT) par des
 * accès mémoire plus performants sur x86.
 *
 * Avantages du MMIO sur x86:
 * - Peut utiliser n'importe quel registre général (pas limité à EAX)
 * - Meilleure performance pour les accès fréquents
 * - Compatible avec les architectures modernes (PCIe)
 *
 * IMPORTANT: Les accès MMIO doivent être:
 * 1. Non-cachables (attribut PAGE_NOCACHE dans les page tables)
 * 2. Ordonnés correctement (utiliser les barriers si nécessaire)
 * 3. De la bonne largeur (8/16/32/64 bits selon le registre)
 */

#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================
 * Types pour les adresses MMIO
 * ======================================== */

/* Adresse MMIO virtuelle (après ioremap) */
typedef volatile void* mmio_addr_t;

/* ========================================
 * Fonctions de lecture MMIO
 * ========================================
 * Ces fonctions garantissent:
 * - La largeur exacte de l'accès mémoire
 * - Pas d'optimisation par le compilateur (volatile)
 * - Ordre des accès préservé
 */

/**
 * Lit un octet (8 bits) depuis une adresse MMIO.
 *
 * @param addr  Adresse MMIO virtuelle
 * @return Valeur 8-bit lue
 */
static inline uint8_t mmio_read8(mmio_addr_t addr)
{
    return *(volatile uint8_t*)addr;
}

/**
 * Lit un mot (16 bits) depuis une adresse MMIO.
 * L'adresse doit être alignée sur 2 octets.
 *
 * @param addr  Adresse MMIO virtuelle (alignée sur 2)
 * @return Valeur 16-bit lue
 */
static inline uint16_t mmio_read16(mmio_addr_t addr)
{
    return *(volatile uint16_t*)addr;
}

/**
 * Lit un double mot (32 bits) depuis une adresse MMIO.
 * L'adresse doit être alignée sur 4 octets.
 *
 * @param addr  Adresse MMIO virtuelle (alignée sur 4)
 * @return Valeur 32-bit lue
 */
static inline uint32_t mmio_read32(mmio_addr_t addr)
{
    return *(volatile uint32_t*)addr;
}

/**
 * Lit un quad mot (64 bits) depuis une adresse MMIO.
 * L'adresse doit être alignée sur 8 octets.
 * Note: Sur x86-32, ceci génère deux accès 32-bit.
 *
 * @param addr  Adresse MMIO virtuelle (alignée sur 8)
 * @return Valeur 64-bit lue
 */
static inline uint64_t mmio_read64(mmio_addr_t addr)
{
    /* Sur x86-32, on doit lire en deux parties */
    volatile uint32_t* ptr = (volatile uint32_t*)addr;
    uint64_t low = ptr[0];
    uint64_t high = ptr[1];
    return low | (high << 32);
}

/* ========================================
 * Fonctions d'écriture MMIO
 * ======================================== */

/**
 * Écrit un octet (8 bits) à une adresse MMIO.
 *
 * @param addr   Adresse MMIO virtuelle
 * @param value  Valeur 8-bit à écrire
 */
static inline void mmio_write8(mmio_addr_t addr, uint8_t value)
{
    *(volatile uint8_t*)addr = value;
}

/**
 * Écrit un mot (16 bits) à une adresse MMIO.
 * L'adresse doit être alignée sur 2 octets.
 *
 * @param addr   Adresse MMIO virtuelle (alignée sur 2)
 * @param value  Valeur 16-bit à écrire
 */
static inline void mmio_write16(mmio_addr_t addr, uint16_t value)
{
    *(volatile uint16_t*)addr = value;
}

/**
 * Écrit un double mot (32 bits) à une adresse MMIO.
 * L'adresse doit être alignée sur 4 octets.
 *
 * @param addr   Adresse MMIO virtuelle (alignée sur 4)
 * @param value  Valeur 32-bit à écrire
 */
static inline void mmio_write32(mmio_addr_t addr, uint32_t value)
{
    *(volatile uint32_t*)addr = value;
}

/**
 * Écrit un quad mot (64 bits) à une adresse MMIO.
 * L'adresse doit être alignée sur 8 octets.
 * Note: Sur x86-32, ceci génère deux accès 32-bit.
 *
 * @param addr   Adresse MMIO virtuelle (alignée sur 8)
 * @param value  Valeur 64-bit à écrire
 */
static inline void mmio_write64(mmio_addr_t addr, uint64_t value)
{
    /* Sur x86-32, on doit écrire en deux parties */
    volatile uint32_t* ptr = (volatile uint32_t*)addr;
    ptr[0] = (uint32_t)(value & 0xFFFFFFFF);
    ptr[1] = (uint32_t)(value >> 32);
}

/* ========================================
 * Barriers de synchronisation MMIO
 * ========================================
 * Sur x86, les accès mémoire sont naturellement ordonnés
 * (modèle TSO - Total Store Order), mais des barriers
 * explicites sont parfois nécessaires pour:
 * - Garantir la visibilité des écritures avant release de spinlock
 * - Synchroniser avec les périphériques DMA
 */

/**
 * Barrier de lecture MMIO.
 * Garantit que toutes les lectures MMIO précédentes sont complétées
 * avant les opérations suivantes.
 *
 * Sur x86, une simple barrier compilateur suffit car les lectures
 * ne sont pas réordonnées par le CPU.
 */
static inline void mmio_rmb(void)
{
    asm volatile("" ::: "memory");
}

/**
 * Barrier d'écriture MMIO.
 * Garantit que toutes les écritures MMIO précédentes sont complétées
 * avant les opérations suivantes.
 *
 * Sur x86, SFENCE garantit l'ordre des stores.
 */
static inline void mmio_wmb(void)
{
    asm volatile("sfence" ::: "memory");
}

/**
 * Barrier complète MMIO.
 * Garantit l'ordre de toutes les opérations mémoire.
 *
 * Sur x86, MFENCE est la barrier la plus forte.
 */
static inline void mmio_mb(void)
{
    asm volatile("mfence" ::: "memory");
}

/**
 * Barrier d'écriture MMIO pour release de spinlock.
 * Garantit que les écritures MMIO sont visibles avant
 * de relâcher un spinlock.
 *
 * Sur x86 avec TSO, une barrier compilateur suffit généralement,
 * mais on utilise SFENCE pour être sûr avec les périphériques.
 */
static inline void mmiowb(void)
{
    asm volatile("sfence" ::: "memory");
}

/* ========================================
 * Macros utilitaires
 * ======================================== */

/**
 * Calcule l'adresse d'un registre MMIO avec offset.
 */
#define MMIO_REG(base, offset) ((mmio_addr_t)((uint8_t*)(base) + (offset)))

/**
 * Lecture avec offset depuis une base MMIO.
 */
#define mmio_read8_off(base, off)   mmio_read8(MMIO_REG(base, off))
#define mmio_read16_off(base, off)  mmio_read16(MMIO_REG(base, off))
#define mmio_read32_off(base, off)  mmio_read32(MMIO_REG(base, off))

/**
 * Écriture avec offset depuis une base MMIO.
 */
#define mmio_write8_off(base, off, val)   mmio_write8(MMIO_REG(base, off), val)
#define mmio_write16_off(base, off, val)  mmio_write16(MMIO_REG(base, off), val)
#define mmio_write32_off(base, off, val)  mmio_write32(MMIO_REG(base, off), val)

/* ========================================
 * Fonctions de mapping MMIO (ioremap)
 * ======================================== */

/**
 * Mappe une région physique MMIO dans l'espace d'adressage virtuel.
 * La région est mappée avec les attributs non-cachables appropriés.
 *
 * @param phys_addr  Adresse physique de début de la région MMIO
 * @param size       Taille de la région en octets
 * @return Adresse virtuelle de la région mappée, ou NULL si échec
 *
 * Note: La région est automatiquement alignée sur PAGE_SIZE (4 KiB).
 * L'appelant doit utiliser iounmap() pour libérer le mapping.
 */
mmio_addr_t ioremap(uint32_t phys_addr, uint32_t size);

/**
 * Libère un mapping MMIO créé par ioremap().
 *
 * @param virt_addr  Adresse virtuelle retournée par ioremap()
 * @param size       Taille de la région (doit correspondre à l'appel ioremap)
 */
void iounmap(mmio_addr_t virt_addr, uint32_t size);

/**
 * Mappe une région MMIO avec des flags spécifiques.
 * Version avancée de ioremap() pour des besoins particuliers.
 *
 * @param phys_addr  Adresse physique de début
 * @param size       Taille de la région
 * @param flags      Flags de page additionnels (ex: PAGE_WRITETHROUGH)
 * @return Adresse virtuelle, ou NULL si échec
 */
mmio_addr_t ioremap_flags(uint32_t phys_addr, uint32_t size, uint32_t flags);

/* ========================================
 * Gestion des régions MMIO
 * ======================================== */

/**
 * Structure décrivant une région MMIO mappée.
 * Utilisée pour le tracking et éviter les conflits.
 */
typedef struct mmio_region {
    uint32_t phys_addr;      /* Adresse physique de base */
    uint32_t virt_addr;      /* Adresse virtuelle mappée */
    uint32_t size;           /* Taille de la région */
    uint32_t flags;          /* Flags de mapping */
    const char* name;        /* Nom descriptif (pour debug) */
    struct mmio_region* next; /* Liste chaînée */
} mmio_region_t;

/**
 * Initialise le sous-système MMIO.
 * Doit être appelé après vmm_init().
 */
void mmio_init(void);

/**
 * Enregistre une région MMIO pour le tracking.
 * Utilisé en interne par ioremap().
 *
 * @param phys_addr  Adresse physique
 * @param virt_addr  Adresse virtuelle
 * @param size       Taille
 * @param name       Nom descriptif
 * @return 0 si succès, -1 si conflit détecté
 */
int mmio_register_region(uint32_t phys_addr, uint32_t virt_addr, 
                         uint32_t size, const char* name);

/**
 * Désenregistre une région MMIO.
 *
 * @param virt_addr  Adresse virtuelle de la région
 */
void mmio_unregister_region(uint32_t virt_addr);

/**
 * Vérifie si une adresse physique est dans une région MMIO enregistrée.
 *
 * @param phys_addr  Adresse physique à vérifier
 * @return true si l'adresse est dans une région MMIO
 */
bool mmio_is_mmio_address(uint32_t phys_addr);

/**
 * Affiche les régions MMIO enregistrées (debug).
 */
void mmio_dump_regions(void);

#endif /* MMIO_H */
