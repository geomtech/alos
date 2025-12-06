/* src/kernel/mmio/pci_mmio.h - Configuration PCI/PCIe pour MMIO
 *
 * Ce module gère la découverte et la configuration des régions MMIO
 * des périphériques PCI. Il parse les BARs (Base Address Registers)
 * pour identifier les régions MMIO vs PIO.
 *
 * Selon la spécification PCI:
 * - BAR bit 0 = 0 : Région MMIO (Memory Space)
 * - BAR bit 0 = 1 : Région PIO (I/O Space)
 *
 * Pour les BARs MMIO:
 * - Bits 1-2 : Type (00 = 32-bit, 10 = 64-bit)
 * - Bit 3    : Prefetchable
 * - Bits 4-31: Adresse de base (alignée sur 16 octets minimum)
 */

#ifndef PCI_MMIO_H
#define PCI_MMIO_H

#include <stdint.h>
#include <stdbool.h>
#include "../../drivers/pci.h"

/* ========================================
 * Constantes PCI BAR
 * ======================================== */

/* Bit 0 du BAR indique le type */
#define PCI_BAR_TYPE_MASK       0x01
#define PCI_BAR_TYPE_MMIO       0x00    /* Memory Space */
#define PCI_BAR_TYPE_PIO        0x01    /* I/O Space */

/* Pour les BARs MMIO, bits 1-2 indiquent la largeur d'adresse */
#define PCI_BAR_MMIO_TYPE_MASK  0x06
#define PCI_BAR_MMIO_32BIT      0x00    /* 32-bit address */
#define PCI_BAR_MMIO_64BIT      0x04    /* 64-bit address (utilise 2 BARs) */

/* Bit 3 : Prefetchable */
#define PCI_BAR_PREFETCHABLE    0x08

/* Masque pour l'adresse MMIO (bits 4-31) */
#define PCI_BAR_MMIO_ADDR_MASK  0xFFFFFFF0

/* Masque pour l'adresse PIO (bits 2-31) */
#define PCI_BAR_PIO_ADDR_MASK   0xFFFFFFFC

/* Nombre maximum de BARs par device */
#define PCI_MAX_BARS            6

/* ========================================
 * Types
 * ======================================== */

/**
 * Type de région BAR.
 */
typedef enum {
    PCI_BAR_NONE = 0,       /* BAR non utilisé */
    PCI_BAR_REGION_MMIO,    /* Région MMIO */
    PCI_BAR_REGION_PIO      /* Région PIO */
} pci_bar_type_t;

/**
 * Structure décrivant une région BAR.
 */
typedef struct {
    pci_bar_type_t type;    /* Type de région (MMIO ou PIO) */
    uint64_t base_addr;     /* Adresse de base physique (64-bit pour BAR 64-bit) */
    uint64_t size;          /* Taille de la région */
    bool is_64bit;          /* true si BAR 64-bit (occupe 2 slots) */
    bool prefetchable;      /* true si prefetchable (MMIO seulement) */
    uint8_t bar_index;      /* Index du BAR (0-5) */
} pci_bar_info_t;

/**
 * Structure contenant toutes les infos BAR d'un device.
 */
typedef struct {
    PCIDevice* pci_dev;                 /* Device PCI associé */
    pci_bar_info_t bars[PCI_MAX_BARS];  /* Informations des BARs */
    int mmio_bar_count;                 /* Nombre de BARs MMIO */
    int pio_bar_count;                  /* Nombre de BARs PIO */
} pci_device_bars_t;

/* ========================================
 * Fonctions publiques
 * ======================================== */

/**
 * Parse tous les BARs d'un device PCI.
 * Remplit la structure pci_device_bars_t avec les informations
 * de chaque BAR (type, adresse, taille).
 *
 * @param pci_dev  Device PCI à analyser
 * @param bars     Structure à remplir avec les infos BAR
 * @return 0 si succès, -1 si erreur
 */
int pci_parse_bars(PCIDevice* pci_dev, pci_device_bars_t* bars);

/**
 * Détermine la taille d'un BAR en écrivant 0xFFFFFFFF et
 * en lisant la valeur résultante.
 *
 * @param pci_dev   Device PCI
 * @param bar_index Index du BAR (0-5)
 * @return Taille de la région en octets
 */
uint64_t pci_get_bar_size(PCIDevice* pci_dev, int bar_index);

/**
 * Vérifie si un BAR est de type MMIO.
 *
 * @param bar_value Valeur brute du BAR
 * @return true si MMIO, false si PIO
 */
static inline bool pci_bar_is_mmio(uint32_t bar_value)
{
    return (bar_value & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_MMIO;
}

/**
 * Vérifie si un BAR est de type PIO.
 *
 * @param bar_value Valeur brute du BAR
 * @return true si PIO, false si MMIO
 */
static inline bool pci_bar_is_pio(uint32_t bar_value)
{
    return (bar_value & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_PIO;
}

/**
 * Extrait l'adresse de base d'un BAR MMIO.
 *
 * @param bar_value Valeur brute du BAR
 * @return Adresse physique de base
 */
static inline uint64_t pci_bar_mmio_addr(uint32_t bar_value)
{
    return (uint64_t)(bar_value & PCI_BAR_MMIO_ADDR_MASK);
}

/**
 * Extrait l'adresse de base d'un BAR PIO.
 *
 * @param bar_value Valeur brute du BAR
 * @return Adresse de port I/O
 */
static inline uint32_t pci_bar_pio_addr(uint32_t bar_value)
{
    return bar_value & PCI_BAR_PIO_ADDR_MASK;  /* PIO reste 16-bit sur x86 */
}

/**
 * Vérifie si un BAR MMIO est 64-bit.
 *
 * @param bar_value Valeur brute du BAR
 * @return true si 64-bit
 */
static inline bool pci_bar_is_64bit(uint32_t bar_value)
{
    return (bar_value & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_64BIT;
}

/**
 * Vérifie si un BAR MMIO est prefetchable.
 *
 * @param bar_value Valeur brute du BAR
 * @return true si prefetchable
 */
static inline bool pci_bar_is_prefetchable(uint32_t bar_value)
{
    return (bar_value & PCI_BAR_PREFETCHABLE) != 0;
}

/**
 * Mappe un BAR MMIO dans l'espace virtuel.
 * Utilise ioremap() en interne.
 *
 * @param bar_info  Information du BAR à mapper
 * @return Adresse virtuelle, ou NULL si échec
 */
void* pci_map_bar(pci_bar_info_t* bar_info);

/**
 * Libère le mapping d'un BAR MMIO.
 *
 * @param virt_addr  Adresse virtuelle retournée par pci_map_bar()
 * @param bar_info   Information du BAR
 */
void pci_unmap_bar(void* virt_addr, pci_bar_info_t* bar_info);

/**
 * Affiche les informations des BARs d'un device (debug).
 *
 * @param bars  Structure contenant les infos BAR
 */
void pci_dump_bars(pci_device_bars_t* bars);

/**
 * Trouve le premier BAR MMIO d'un device.
 *
 * @param bars  Structure contenant les infos BAR
 * @return Pointeur vers le bar_info, ou NULL si aucun MMIO
 */
pci_bar_info_t* pci_find_mmio_bar(pci_device_bars_t* bars);

/**
 * Trouve le premier BAR PIO d'un device.
 *
 * @param bars  Structure contenant les infos BAR
 * @return Pointeur vers le bar_info, ou NULL si aucun PIO
 */
pci_bar_info_t* pci_find_pio_bar(pci_device_bars_t* bars);

#endif /* PCI_MMIO_H */
