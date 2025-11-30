/* src/pci.h - PCI Bus Driver */
#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Ports PCI Configuration Space */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* Offsets standards dans le PCI Configuration Space */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS           0x0B
#define PCI_CACHE_LINE_SIZE 0x0C
#define PCI_LATENCY_TIMER   0x0D
#define PCI_HEADER_TYPE     0x0E
#define PCI_BIST            0x0F
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D

/* Vendor IDs connus */
#define PCI_VENDOR_AMD      0x1022
#define PCI_VENDOR_INTEL    0x8086
#define PCI_VENDOR_NVIDIA   0x10DE
#define PCI_VENDOR_REALTEK  0x10EC
#define PCI_VENDOR_QEMU     0x1234

/* Device IDs connus */
#define PCI_DEVICE_AMD_PCNET 0x2000

/* Classes PCI */
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_MEMORY        0x05
#define PCI_CLASS_BRIDGE        0x06

/**
 * Structure représentant un périphérique PCI détecté.
 */
typedef struct PCIDevice {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  interrupt_line;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    struct PCIDevice* next;
} PCIDevice;

/**
 * Lit un mot de 16 bits depuis le PCI Configuration Space.
 * 
 * @param bus    Numéro du bus (0-255)
 * @param slot   Numéro du slot/device (0-31)
 * @param func   Numéro de fonction (0-7)
 * @param offset Offset dans le configuration space (doit être pair)
 * @return Valeur 16-bit lue
 */
uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * Lit un double mot de 32 bits depuis le PCI Configuration Space.
 */
uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * Écrit un double mot de 32 bits dans le PCI Configuration Space.
 */
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/**
 * Scanne tous les bus PCI et énumère les périphériques trouvés.
 * Les périphériques sont ajoutés à une liste chaînée globale.
 */
void pci_probe(void);

/**
 * Recherche un périphérique PCI par vendor_id et device_id.
 * 
 * @param vendor_id ID du fabricant
 * @param device_id ID du périphérique
 * @return Pointeur vers le PCIDevice ou NULL si non trouvé
 */
PCIDevice* pci_get_device(uint16_t vendor_id, uint16_t device_id);

/**
 * Recherche un périphérique PCI par classe.
 * 
 * @param class_code Classe PCI (ex: 0x02 pour Network)
 * @param subclass   Sous-classe
 * @return Pointeur vers le premier PCIDevice correspondant ou NULL
 */
PCIDevice* pci_get_device_by_class(uint8_t class_code, uint8_t subclass);

/**
 * Retourne la liste de tous les périphériques PCI détectés.
 */
PCIDevice* pci_get_devices(void);

/**
 * Retourne le nombre de périphériques PCI détectés.
 */
int pci_get_device_count(void);

/**
 * Retourne le nom d'un vendor connu (ou "Unknown").
 */
const char* pci_get_vendor_name(uint16_t vendor_id);

/**
 * Retourne le nom d'une classe PCI.
 */
const char* pci_get_class_name(uint8_t class_code);

#endif /* PCI_H */
