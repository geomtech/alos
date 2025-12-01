/* src/multiboot.h */
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/* Le Magic Number que GRUB place dans EAX après le boot */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Flags de la structure Multiboot Info */
#define MULTIBOOT_INFO_MEMORY       0x00000001  /* mem_lower et mem_upper valides */
#define MULTIBOOT_INFO_BOOTDEV      0x00000002  /* boot_device valide */
#define MULTIBOOT_INFO_CMDLINE      0x00000004  /* cmdline valide */
#define MULTIBOOT_INFO_MODS         0x00000008  /* mods_count et mods_addr valides */
#define MULTIBOOT_INFO_AOUT_SYMS    0x00000010  /* aout_sym valide */
#define MULTIBOOT_INFO_ELF_SHDR     0x00000020  /* elf_sec valide */
#define MULTIBOOT_INFO_MEM_MAP      0x00000040  /* mmap_length et mmap_addr valides */
#define MULTIBOOT_INFO_DRIVE_INFO   0x00000080  /* drives valides */
#define MULTIBOOT_INFO_CONFIG_TABLE 0x00000100  /* config_table valide */
#define MULTIBOOT_INFO_BOOT_LOADER  0x00000200  /* boot_loader_name valide */
#define MULTIBOOT_INFO_APM_TABLE    0x00000400  /* apm_table valide */
#define MULTIBOOT_INFO_VBE_INFO     0x00000800  /* vbe_* valides */
#define MULTIBOOT_INFO_FRAMEBUFFER  0x00001000  /* framebuffer_* valides */

/* Structure d'une entrée de la memory map */
typedef struct multiboot_mmap_entry {
    uint32_t size;      /* Taille de cette entrée (sans compter ce champ) */
    uint64_t addr;      /* Adresse de début de la région */
    uint64_t len;       /* Longueur de la région en octets */
    uint32_t type;      /* Type de la région */
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5
} __attribute__((packed)) multiboot_mmap_entry_t;

/* Structure d'un module chargé par GRUB */
typedef struct multiboot_mod {
    uint32_t mod_start;  /* Adresse de début du module */
    uint32_t mod_end;    /* Adresse de fin du module */
    uint32_t cmdline;    /* Ligne de commande du module (chaîne C) */
    uint32_t reserved;   /* Réservé, doit être 0 */
} __attribute__((packed)) multiboot_mod_t;

/* Structure principale Multiboot Info */
typedef struct multiboot_info {
    /* Flags indiquant quels champs sont valides */
    uint32_t flags;

    /* Mémoire basse et haute en KiB (si flags & MULTIBOOT_INFO_MEMORY) */
    uint32_t mem_lower;  /* Mémoire conventionnelle (0-640KB) */
    uint32_t mem_upper;  /* Mémoire étendue (au-dessus de 1MB) */

    /* Périphérique de boot (si flags & MULTIBOOT_INFO_BOOTDEV) */
    uint32_t boot_device;

    /* Ligne de commande du kernel (si flags & MULTIBOOT_INFO_CMDLINE) */
    uint32_t cmdline;

    /* Modules chargés (si flags & MULTIBOOT_INFO_MODS) */
    uint32_t mods_count;
    uint32_t mods_addr;

    /* Symboles (union, selon le format ELF ou a.out) */
    union {
        struct {
            uint32_t tabsize;
            uint32_t strsize;
            uint32_t addr;
            uint32_t reserved;
        } aout_sym;
        struct {
            uint32_t num;
            uint32_t size;
            uint32_t addr;
            uint32_t shndx;
        } elf_sec;
    } syms;

    /* Memory map (si flags & MULTIBOOT_INFO_MEM_MAP) */
    uint32_t mmap_length;
    uint32_t mmap_addr;

    /* Drives (si flags & MULTIBOOT_INFO_DRIVE_INFO) */
    uint32_t drives_length;
    uint32_t drives_addr;

    /* Table de configuration ROM (si flags & MULTIBOOT_INFO_CONFIG_TABLE) */
    uint32_t config_table;

    /* Nom du bootloader (si flags & MULTIBOOT_INFO_BOOT_LOADER) */
    uint32_t boot_loader_name;

    /* Table APM (si flags & MULTIBOOT_INFO_APM_TABLE) */
    uint32_t apm_table;

    /* Informations VBE (si flags & MULTIBOOT_INFO_VBE_INFO) */
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    /* Informations framebuffer (si flags & MULTIBOOT_INFO_FRAMEBUFFER) */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    /* Champs de couleur dépendent du type */
    union {
        struct {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        } palette;
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        } rgb;
    } fb_colors;
} __attribute__((packed)) multiboot_info_t;

#endif /* MULTIBOOT_H */
