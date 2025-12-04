/* src/kernel/elf.c - ELF Loader Implementation */
#include "elf.h"
#include "console.h"
#include "klog.h"
#include "../fs/vfs.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "../include/elf.h"

/* ========================================
 * Fonctions utilitaires locales
 * ======================================== */

/**
 * Copie n octets de src vers dst.
 */
static void elf_memcpy(void* dst, const void* src, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

/**
 * Met à zéro n octets à partir de dst.
 */
static void elf_memset(void* dst, uint8_t val, uint32_t n)
{
    uint8_t* d = (uint8_t*)dst;
    while (n--) {
        *d++ = val;
    }
}

/**
 * Vérifie le magic number ELF.
 */
static int elf_check_magic(uint8_t* e_ident)
{
    return (e_ident[EI_MAG0] == ELF_MAGIC_0 &&
            e_ident[EI_MAG1] == ELF_MAGIC_1 &&
            e_ident[EI_MAG2] == ELF_MAGIC_2 &&
            e_ident[EI_MAG3] == ELF_MAGIC_3);
}

/**
 * Valide un header ELF64 pour notre système (x86-64).
 */
static int elf_validate_header64(Elf64_Ehdr* ehdr)
{
    /* Vérifier le magic number */
    if (!elf_check_magic(ehdr->e_ident)) {
        KLOG_ERROR("ELF", "Invalid magic number");
        return ELF_ERR_MAGIC;
    }
    
    /* Vérifier la classe (64-bit) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        KLOG_ERROR("ELF", "Not a 64-bit ELF");
        return ELF_ERR_CLASS;
    }
    
    /* Vérifier l'endianness (little endian) */
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        KLOG_ERROR("ELF", "Not little-endian");
        return ELF_ERR_CLASS;
    }
    
    /* Vérifier le type (exécutable) */
    if (ehdr->e_type != ET_EXEC) {
        KLOG_ERROR("ELF", "Not an executable");
        return ELF_ERR_TYPE;
    }
    
    /* Vérifier la machine (x86-64) */
    if (ehdr->e_machine != EM_X86_64) {
        KLOG_ERROR("ELF", "Not for x86-64 architecture");
        return ELF_ERR_MACHINE;
    }
    
    return ELF_OK;
}

/* ========================================
 * Fonctions publiques
 * ======================================== */

int elf_load_file(const char* filename, process_t* proc, elf_load_result_t* result)
{
    KLOG_INFO("ELF", "=== Loading ELF64 ===");
    KLOG_INFO("ELF", filename);
    
    /* Déterminer le Page Directory cible */
    page_directory_t* target_dir = NULL;
    if (proc != NULL && proc->pml4 != NULL) {
        target_dir = (page_directory_t*)proc->pml4;
        KLOG_INFO("ELF", "Loading into process-specific page directory");
    } else {
        target_dir = vmm_get_kernel_directory();
        KLOG_INFO("ELF", "Loading into kernel page directory");
    }
    
    /* Ouvrir le fichier via VFS */
    vfs_node_t* file = vfs_open(filename, VFS_O_RDONLY);
    if (file == NULL) {
        KLOG_ERROR("ELF", "File not found");
        return ELF_ERR_FILE;
    }
    
    /* Lire le header ELF64 */
    Elf64_Ehdr ehdr;
    int bytes_read = vfs_read(file, 0, sizeof(Elf64_Ehdr), (uint8_t*)&ehdr);
    if (bytes_read != (int)sizeof(Elf64_Ehdr)) {
        KLOG_ERROR("ELF", "Failed to read ELF header");
        vfs_close(file);
        return ELF_ERR_FILE;
    }
    
    /* Valider le header */
    int err = elf_validate_header64(&ehdr);
    if (err != ELF_OK) {
        vfs_close(file);
        return err;
    }
    
    KLOG_INFO_HEX("ELF", "Entry point: ", ehdr.e_entry);
    KLOG_INFO_DEC("ELF", "Program headers: ", ehdr.e_phnum);
    
    /* Initialiser le résultat */
    if (result != NULL) {
        result->entry_point = ehdr.e_entry;
        result->base_addr = 0xFFFFFFFFFFFFFFFF;
        result->top_addr = 0;
        result->num_segments = 0;
    }
    
    /* Allouer un buffer pour les Program Headers */
    uint64_t phdr_size = ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr* phdrs = (Elf64_Phdr*)kmalloc(phdr_size);
    if (phdrs == NULL) {
        KLOG_ERROR("ELF", "Failed to allocate phdr buffer");
        vfs_close(file);
        return ELF_ERR_MEMORY;
    }
    
    /* Lire les Program Headers */
    bytes_read = vfs_read(file, ehdr.e_phoff, phdr_size, (uint8_t*)phdrs);
    if (bytes_read != (int)phdr_size) {
        KLOG_ERROR("ELF", "Failed to read program headers");
        kfree(phdrs);
        vfs_close(file);
        return ELF_ERR_FILE;
    }
    
    /* Parcourir les Program Headers */
    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr* phdr = &phdrs[i];
        
        /* On ne charge que les segments PT_LOAD */
        if (phdr->p_type != PT_LOAD) {
            continue;
        }
        
        KLOG_INFO("ELF", "--- PT_LOAD Segment ---");
        KLOG_INFO_HEX("ELF", "  VAddr:  ", phdr->p_vaddr);
        KLOG_INFO_HEX("ELF", "  FileSz: ", phdr->p_filesz);
        KLOG_INFO_HEX("ELF", "  MemSz:  ", phdr->p_memsz);
        KLOG_INFO_HEX("ELF", "  Flags:  ", phdr->p_flags);
        
        /* Calculer les pages nécessaires */
        uint64_t vaddr_start = PAGE_ALIGN_DOWN(phdr->p_vaddr);
        uint64_t vaddr_end = PAGE_ALIGN_UP(phdr->p_vaddr + phdr->p_memsz);
        uint64_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;
        
        KLOG_INFO_DEC("ELF", "  Pages needed: ", num_pages);
        
        /* Allouer et mapper les pages */
        for (uint64_t page = 0; page < num_pages; page++) {
            uint64_t virt_addr = vaddr_start + (page * PAGE_SIZE);
            
            /* Vérifier si la page n'est pas déjà mappée */
            if (!vmm_is_mapped_in_dir(target_dir, virt_addr)) {
                /* Allouer une page physique */
                void* phys_page = pmm_alloc_block();
                if (phys_page == NULL) {
                    KLOG_ERROR("ELF", "Out of physical memory!");
                    kfree(phdrs);
                    vfs_close(file);
                    return ELF_ERR_MEMORY;
                }
                
                /* Déterminer les flags de la page */
                uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
                if (phdr->p_flags & PF_W) {
                    page_flags |= PAGE_RW;
                }
                
                /* Mapper la page dans le directory cible */
                if (vmm_map_page_in_dir(target_dir, (uint64_t)phys_page, virt_addr, page_flags) != 0) {
                    KLOG_ERROR("ELF", "Failed to map page!");
                    pmm_free_block(phys_page);
                    kfree(phdrs);
                    vfs_close(file);
                    return ELF_ERR_MEMORY;
                }
                
                /* Mettre la page à zéro */
                if (vmm_memset_in_dir(target_dir, virt_addr, 0, PAGE_SIZE) != 0) {
                    KLOG_ERROR("ELF", "Failed to zero page!");
                    kfree(phdrs);
                    vfs_close(file);
                    return ELF_ERR_MEMORY;
                }
            }
        }
        
        /* Copier les données du fichier vers la mémoire */
        if (phdr->p_filesz > 0) {
            /* Allouer un buffer temporaire pour lire le segment */
            uint8_t* seg_buffer = (uint8_t*)kmalloc(phdr->p_filesz);
            if (seg_buffer == NULL) {
                KLOG_ERROR("ELF", "Failed to allocate segment buffer");
                kfree(phdrs);
                vfs_close(file);
                return ELF_ERR_MEMORY;
            }
            
            /* Lire le segment depuis le fichier */
            bytes_read = vfs_read(file, phdr->p_offset, phdr->p_filesz, seg_buffer);
            if (bytes_read != (int)phdr->p_filesz) {
                KLOG_ERROR("ELF", "Failed to read segment data");
                kfree(seg_buffer);
                kfree(phdrs);
                vfs_close(file);
                return ELF_ERR_FILE;
            }
            
            /* Copier vers l'adresse virtuelle dans le directory cible */
            if (vmm_copy_to_dir(target_dir, phdr->p_vaddr, seg_buffer, phdr->p_filesz) != 0) {
                KLOG_ERROR("ELF", "Failed to copy segment data!");
                kfree(seg_buffer);
                kfree(phdrs);
                vfs_close(file);
                return ELF_ERR_MEMORY;
            }
            
            kfree(seg_buffer);
        }
        
        /* Le .bss (p_memsz > p_filesz) est déjà à zéro grâce au memset précédent */
        
        /* Mettre à jour les statistiques */
        if (result != NULL) {
            if (phdr->p_vaddr < result->base_addr) {
                result->base_addr = phdr->p_vaddr;
            }
            if (phdr->p_vaddr + phdr->p_memsz > result->top_addr) {
                result->top_addr = phdr->p_vaddr + phdr->p_memsz;
            }
            result->num_segments++;
        }
    }
    
    /* Nettoyer */
    kfree(phdrs);
    vfs_close(file);
    
    KLOG_INFO("ELF", "=== ELF Loaded Successfully ===");
    
    /* Stocker le point d'entrée dans le processus si fourni */
    (void)proc;  /* Pour l'instant on n'utilise pas proc directement */
    
    return ELF_OK;
}

int elf_is_valid(const char* filename)
{
    vfs_node_t* file = vfs_open(filename, VFS_O_RDONLY);
    if (file == NULL) {
        return 0;
    }
    
    Elf64_Ehdr ehdr;
    int bytes_read = vfs_read(file, 0, sizeof(Elf64_Ehdr), (uint8_t*)&ehdr);
    vfs_close(file);
    
    if (bytes_read != (int)sizeof(Elf64_Ehdr)) {
        return 0;
    }
    
    return (elf_validate_header64(&ehdr) == ELF_OK);
}

void elf_info(const char* filename)
{
    vfs_node_t* file = vfs_open(filename, VFS_O_RDONLY);
    if (file == NULL) {
        console_puts("Error: File not found\n");
        return;
    }
    
    Elf64_Ehdr ehdr;
    int bytes_read = vfs_read(file, 0, sizeof(Elf64_Ehdr), (uint8_t*)&ehdr);
    if (bytes_read != (int)sizeof(Elf64_Ehdr)) {
        console_puts("Error: Could not read ELF header\n");
        vfs_close(file);
        return;
    }
    
    console_puts("\n=== ELF File Info ===\n");
    console_puts("File: ");
    console_puts(filename);
    console_puts("\n");
    
    /* Magic */
    console_puts("Magic: ");
    if (elf_check_magic(ehdr.e_ident)) {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        console_puts("Valid (0x7F ELF)\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("INVALID\n");
    }
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    /* Class */
    console_puts("Class: ");
    if (ehdr.e_ident[EI_CLASS] == ELFCLASS32) {
        console_puts("32-bit\n");
    } else if (ehdr.e_ident[EI_CLASS] == ELFCLASS64) {
        console_puts("64-bit\n");
    } else {
        console_puts("Unknown\n");
    }
    
    /* Type */
    console_puts("Type: ");
    switch (ehdr.e_type) {
        case ET_EXEC: console_puts("Executable\n"); break;
        case ET_REL:  console_puts("Relocatable\n"); break;
        case ET_DYN:  console_puts("Shared Object\n"); break;
        default:      console_puts("Other\n"); break;
    }
    
    /* Machine */
    console_puts("Machine: ");
    if (ehdr.e_machine == EM_386) {
        console_puts("i386\n");
    } else if (ehdr.e_machine == EM_X86_64) {
        console_puts("x86_64\n");
    } else {
        console_puts("Other (");
        console_put_dec(ehdr.e_machine);
        console_puts(")\n");
    }
    
    /* Entry point */
    console_puts("Entry Point: 0x");
    console_put_hex(ehdr.e_entry);
    console_puts("\n");
    
    /* Program headers */
    console_puts("Program Headers: ");
    console_put_dec(ehdr.e_phnum);
    console_puts(" (offset: 0x");
    console_put_hex(ehdr.e_phoff);
    console_puts(")\n");
    
    /* Section headers */
    console_puts("Section Headers: ");
    console_put_dec(ehdr.e_shnum);
    console_puts(" (offset: 0x");
    console_put_hex(ehdr.e_shoff);
    console_puts(")\n");
    
    vfs_close(file);
}
