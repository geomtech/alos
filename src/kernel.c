/* src/kernel.c */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "multiboot.h"
#include "pmm.h"

/* Adresse physique du buffer VGA en mode texte couleur */
static uint16_t *const VGA_MEMORY = (uint16_t *)0xB8000;
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

/* Variables globales pour les infos Multiboot */
static multiboot_info_t *g_mboot_info = NULL;
static uint32_t g_mboot_magic = 0;

/* Génère un octet de couleur (4 bits fond, 4 bits texte) */
static inline uint8_t vga_color(uint8_t fg, uint8_t bg)
{
    return fg | bg << 4;
}

/* Génère un mot de 16 bits (caractère + couleur) pour la VRAM */
static inline uint16_t vga_entry(unsigned char uc, uint8_t color)
{
    return (uint16_t)uc | (uint16_t)color << 8;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

void timer_handler_c(void)
{
    // On ne fait rien pour l'instant, mais IL FAUT acquitter.
    // Envoyer EOI au PIC Maître
    outb(0x20, 0x20);
}

/* Fonction helper pour afficher un nombre en hexadécimal */
static void print_hex(uint32_t value, size_t offset, uint8_t color)
{
    const char hex_chars[] = "0123456789ABCDEF";
    VGA_MEMORY[offset++] = vga_entry('0', color);
    VGA_MEMORY[offset++] = vga_entry('x', color);
    for (int i = 7; i >= 0; i--) {
        VGA_MEMORY[offset++] = vga_entry(hex_chars[(value >> (i * 4)) & 0xF], color);
    }
}

/* Fonction helper pour afficher un nombre décimal */
static void print_dec(uint32_t value, size_t offset, uint8_t color)
{
    char buffer[12];
    int i = 0;
    
    if (value == 0) {
        VGA_MEMORY[offset] = vga_entry('0', color);
        return;
    }
    
    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while (i > 0) {
        VGA_MEMORY[offset++] = vga_entry(buffer[--i], color);
    }
}

/* Fonction helper pour afficher une chaîne */
static void print_string(const char *str, size_t offset, uint8_t color)
{
    while (*str) {
        VGA_MEMORY[offset++] = vga_entry(*str++, color);
    }
}

void kernel_main(uint32_t magic, multiboot_info_t *mboot_info)
{
    /* Sauvegarder les infos Multiboot */
    g_mboot_magic = magic;
    g_mboot_info = mboot_info;

    init_gdt();
    init_idt();

    // 1. Nettoyage de l'écran (Fond Bleu, Texte Blanc pour le style "BSOD")
    uint8_t terminal_color = vga_color(15, 1); // 15=White, 1=Blue
    for (size_t y = 0; y < VGA_HEIGHT; y++)
    {
        for (size_t x = 0; x < VGA_WIDTH; x++)
        {
            const size_t index = y * VGA_WIDTH + x;
            VGA_MEMORY[index] = vga_entry(' ', terminal_color);
        }
    }

    // 2. Vérifier le magic number Multiboot
    size_t line = 2;
    uint8_t error_color = vga_color(12, 1);  // Red on Blue
    uint8_t info_color = vga_color(14, 1);   // Yellow on Blue
    
    print_string("AuraOS - Multiboot Info", line * VGA_WIDTH + 2, terminal_color);
    line += 2;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        print_string("ERROR: Invalid Multiboot magic number!", line * VGA_WIDTH + 2, error_color);
        line++;
        print_string("Expected: ", line * VGA_WIDTH + 2, error_color);
        print_hex(MULTIBOOT_BOOTLOADER_MAGIC, line * VGA_WIDTH + 12, error_color);
        line++;
        print_string("Got: ", line * VGA_WIDTH + 2, error_color);
        print_hex(magic, line * VGA_WIDTH + 7, error_color);
    } else {
        print_string("Multiboot magic OK: ", line * VGA_WIDTH + 2, info_color);
        print_hex(magic, line * VGA_WIDTH + 22, info_color);
        line += 2;

        // Afficher les infos mémoire si disponibles
        if (mboot_info->flags & MULTIBOOT_INFO_MEMORY) {
            print_string("Memory Lower: ", line * VGA_WIDTH + 2, terminal_color);
            print_dec(mboot_info->mem_lower, line * VGA_WIDTH + 16, terminal_color);
            print_string(" KB", line * VGA_WIDTH + 22, terminal_color);
            line++;
            
            print_string("Memory Upper: ", line * VGA_WIDTH + 2, terminal_color);
            print_dec(mboot_info->mem_upper, line * VGA_WIDTH + 16, terminal_color);
            print_string(" KB", line * VGA_WIDTH + 26, terminal_color);
            line++;
            
            // Calculer la RAM totale approximative
            uint32_t total_mb = (mboot_info->mem_lower + mboot_info->mem_upper + 1024) / 1024;
            print_string("Total RAM: ~", line * VGA_WIDTH + 2, info_color);
            print_dec(total_mb, line * VGA_WIDTH + 14, info_color);
            print_string(" MB", line * VGA_WIDTH + 20, info_color);
            line++;
        }

        line++;
        
        // Afficher le nom du bootloader si disponible
        if (mboot_info->flags & MULTIBOOT_INFO_BOOT_LOADER) {
            print_string("Bootloader: ", line * VGA_WIDTH + 2, terminal_color);
            print_string((const char *)mboot_info->boot_loader_name, line * VGA_WIDTH + 14, terminal_color);
            line++;
        }

        // Afficher les flags disponibles
        line++;
        print_string("Flags: ", line * VGA_WIDTH + 2, terminal_color);
        print_hex(mboot_info->flags, line * VGA_WIDTH + 9, terminal_color);
        line += 2;

        // Initialiser le Physical Memory Manager
        init_pmm(mboot_info);
        
        // Afficher les statistiques du PMM
        uint8_t pmm_color = vga_color(10, 1);  // Green on Blue
        print_string("=== Physical Memory Manager ===", line * VGA_WIDTH + 2, pmm_color);
        line++;
        
        print_string("Total blocks: ", line * VGA_WIDTH + 2, terminal_color);
        print_dec(pmm_get_total_blocks(), line * VGA_WIDTH + 16, terminal_color);
        line++;
        
        print_string("Used blocks:  ", line * VGA_WIDTH + 2, terminal_color);
        print_dec(pmm_get_used_blocks(), line * VGA_WIDTH + 16, terminal_color);
        line++;
        
        print_string("Free blocks:  ", line * VGA_WIDTH + 2, terminal_color);
        print_dec(pmm_get_free_blocks(), line * VGA_WIDTH + 16, terminal_color);
        line++;
        
        print_string("Free memory:  ", line * VGA_WIDTH + 2, terminal_color);
        print_dec(pmm_get_free_memory() / 1024, line * VGA_WIDTH + 16, terminal_color);
        print_string(" KiB", line * VGA_WIDTH + 24, terminal_color);
        line += 2;
        
        // Test d'allocation PMM
        print_string("PMM Test:", line * VGA_WIDTH + 2, info_color);
        line++;
        
        void* block1 = pmm_alloc_block();
        print_string("Alloc #1: ", line * VGA_WIDTH + 2, terminal_color);
        print_hex((uint32_t)(uintptr_t)block1, line * VGA_WIDTH + 12, terminal_color);
        line++;
        
        void* block2 = pmm_alloc_block();
        print_string("Alloc #2: ", line * VGA_WIDTH + 2, terminal_color);
        print_hex((uint32_t)(uintptr_t)block2, line * VGA_WIDTH + 12, terminal_color);
        line++;
        
        pmm_free_block(block1);
        print_string("Freed #1, free blocks: ", line * VGA_WIDTH + 2, terminal_color);
        print_dec(pmm_get_free_blocks(), line * VGA_WIDTH + 25, terminal_color);
        line++;
        
        void* block3 = pmm_alloc_block();
        print_string("Alloc #3: ", line * VGA_WIDTH + 2, terminal_color);
        print_hex((uint32_t)(uintptr_t)block3, line * VGA_WIDTH + 12, terminal_color);
        print_string(" (reused #1)", line * VGA_WIDTH + 23, pmm_color);
    }

    asm volatile("sti");

    while (1)
    {
        // Le CPU tourne ici en attendant que vous appuyiez sur une touche
        asm volatile("hlt"); // Met le CPU en pause pour économiser l'énergie
    }
}