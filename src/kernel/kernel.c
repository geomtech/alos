/* src/kernel/kernel.c */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/io.h"
#include "../include/multiboot.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "console.h"
#include "../drivers/pci.h"
#include "../drivers/net/pcnet.h"
#include "../net/core/netdev.h"
#include "../net/core/net.h"
#include "../net/l3/route.h"

/* Variables globales pour les infos Multiboot */
static multiboot_info_t *g_mboot_info = NULL;
static uint32_t g_mboot_magic = 0;

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

void timer_handler_c(void)
{
    outb(0x20, 0x20);
}

void kernel_main(uint32_t magic, multiboot_info_t *mboot_info)
{
    /* Sauvegarder les infos Multiboot */
    g_mboot_magic = magic;
    g_mboot_info = mboot_info;

    init_gdt();
    init_idt();

    /* Initialiser la console avec scrolling */
    console_init();
    console_clear(VGA_COLOR_BLUE);

    /* Titre */
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    console_puts("ALOS - Multiboot Info\n\n");

    /* Vérifier le magic number */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("ERROR: Invalid Multiboot magic!\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
        console_puts("Multiboot magic OK: ");
        console_put_hex(magic);
        console_puts("\n\n");

        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        
        /* Infos mémoire */
        if (mboot_info->flags & MULTIBOOT_INFO_MEMORY) {
            console_puts("Memory Lower: ");
            console_put_dec(mboot_info->mem_lower);
            console_puts(" KB\n");
            
            console_puts("Memory Upper: ");
            console_put_dec(mboot_info->mem_upper);
            console_puts(" KB\n");
            
            uint32_t total_mb = (mboot_info->mem_lower + mboot_info->mem_upper + 1024) / 1024;
            console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
            console_puts("Total RAM: ~");
            console_put_dec(total_mb);
            console_puts(" MB\n");
        }
        
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        
        /* Bootloader */
        if (mboot_info->flags & MULTIBOOT_INFO_BOOT_LOADER) {
            console_puts("Bootloader: ");
            console_puts((const char *)mboot_info->boot_loader_name);
            console_puts("\n");
        }
        
        console_puts("Flags: ");
        console_put_hex(mboot_info->flags);
        console_puts("\n\n");

        /* ============================================ */
        /* Initialisation du PMM                       */
        /* ============================================ */
        init_pmm(mboot_info);
        
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
        console_puts("=== Physical Memory Manager ===\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        console_puts("Free blocks: ");
        console_put_dec(pmm_get_free_blocks());
        console_puts(" (");
        console_put_dec(pmm_get_free_memory() / 1024);
        console_puts(" KiB)\n\n");

        /* ============================================ */
        /* Initialisation du Kernel Heap               */
        /* ============================================ */
        
        #define HEAP_PAGES 256  /* 1 MiB */
        void* heap_mem = pmm_alloc_blocks(HEAP_PAGES);
        
        if (heap_mem == NULL) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            console_puts("ERROR: Failed to allocate heap memory!\n");
        } else {
            kheap_init(heap_mem, HEAP_PAGES * PMM_BLOCK_SIZE);
            
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
            console_puts("=== Kernel Heap (kmalloc) ===\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            console_puts("Heap start: ");
            console_put_hex((uint32_t)(uintptr_t)heap_mem);
            console_puts("  Size: ");
            console_put_dec(kheap_get_total_size() / 1024);
            console_puts(" KiB\n");
            
            console_puts("Header size: ");
            console_put_dec(sizeof(KHeapBlock));
            console_puts(" bytes (overhead per alloc)\n\n");
            
            /* ============================================ */
            /* Test kmalloc                                 */
            /* ============================================ */
            console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
            console_puts("--- kmalloc Test ---\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Allocation 1 */
            char* str1 = (char*)kmalloc(32);
            console_puts("str1 (32B):  ");
            console_put_hex((uint32_t)(uintptr_t)str1);
            console_puts("\n");
            
            /* Allocation 2 */
            int* arr = (int*)kmalloc(10 * sizeof(int));
            console_puts("arr (40B):   ");
            console_put_hex((uint32_t)(uintptr_t)arr);
            uint32_t gap1 = (uint32_t)(uintptr_t)arr - (uint32_t)(uintptr_t)str1;
            console_puts("  gap: ");
            console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
            console_put_dec(gap1);
            console_puts(" B\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Allocation 3 */
            void* data = kmalloc(16);
            console_puts("data (16B):  ");
            console_put_hex((uint32_t)(uintptr_t)data);
            uint32_t gap2 = (uint32_t)(uintptr_t)data - (uint32_t)(uintptr_t)arr;
            console_puts("  gap: ");
            console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
            console_put_dec(gap2);
            console_puts(" B\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            console_puts("Blocks: ");
            console_put_dec(kheap_get_block_count());
            console_puts("  Free: ");
            console_put_dec(kheap_get_free_size());
            console_puts(" B\n\n");
            
            /* ============================================ */
            /* Test kfree                                   */
            /* ============================================ */
            console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
            console_puts("--- kfree Test ---\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Libérer arr (celui du milieu) */
            kfree(arr);
            console_puts("Freed arr, blocks: ");
            console_put_dec(kheap_get_block_count());
            console_puts("\n");
            
            /* Réallouer */
            void* reused = kmalloc(36);
            console_puts("reused(36B): ");
            console_put_hex((uint32_t)(uintptr_t)reused);
            if (reused == arr) {
                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                console_puts("  REUSED!\n");
            } else {
                console_puts("\n");
            }
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Test coalesce */
            kfree(str1);
            kfree(reused);
            console_puts("Freed str1+reused, blocks: ");
            console_put_dec(kheap_get_block_count());
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
            console_puts(" (coalesced!)\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Nettoyage final */
            kfree(data);
            console_puts("Freed all, blocks: ");
            console_put_dec(kheap_get_block_count());
            console_puts("  Free: ");
            console_put_dec(kheap_get_free_size());
            console_puts(" B\n");
            
            /* ============================================ */
            /* PCI Bus Enumeration                          */
            /* ============================================ */
            pci_probe();
            
            /* ============================================ */
            /* Network Device Initialization                */
            /* ============================================ */
            int num_netdevs = netdev_init();
            if (num_netdevs > 0) {
                /* Initialiser la couche réseau avec la MAC du périphérique par défaut */
                uint8_t mac[6];
                netdev_get_mac(mac);
                net_init(mac);
                
                /* Initialiser la table de routage */
                route_init();
                
                /* Démarrer la carte PCnet si c'est le driver utilisé */
                netdev_t* dev = netdev_get_default();
                if (dev != NULL && dev->type == NETDEV_TYPE_PCNET) {
                    PCNetDevice* pcnet_dev = (PCNetDevice*)dev->driver_data;
                    if (pcnet_start(pcnet_dev)) {
                        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                        console_puts("[NET] Network stack ready!\n");
                        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                    }
                }
            }
        }
        
        /* Instructions */
        console_puts("\n");
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
        console_puts("========================================\n");
        console_puts("Use UP/DOWN arrows to scroll\n");
        console_puts("========================================\n");
    }

    console_refresh();
    
    asm volatile("sti");

    while (1)
    {
        asm volatile("hlt");
    }
}