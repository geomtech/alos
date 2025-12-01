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
#include "timer.h"
#include "../drivers/pci.h"
#include "../drivers/ata.h"
#include "../drivers/net/pcnet.h"
#include "../fs/vfs.h"
#include "../fs/ext2.h"
#include "../net/core/netdev.h"
#include "../net/core/net.h"
#include "../net/l3/route.h"
#include "../net/l3/icmp.h"
#include "../net/l4/dhcp.h"
#include "../net/l4/dns.h"

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

/* Fonction externe pour incrémenter les ticks */
extern void timer_tick(void);

void timer_handler_c(void)
{
    /* Incrémenter le compteur de ticks */
    timer_tick();
    
    /* Envoyer EOI au PIC */
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
        /* Initialisation du Timer (PIT + RTC)         */
        /* ============================================ */
        timer_init(TIMER_FREQUENCY);  /* 1000 Hz = 1ms par tick */
        
        /* Afficher la date/heure de boot */
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
        console_puts("Boot time: ");
        timestamp_print_now();
        console_puts("\n\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);

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
            /* ATA/IDE Disk Driver                          */
            /* ============================================ */
            int disk_ready = 0;
            if (ata_init() == 0) {
                disk_ready = 1;
            }
            
            /* ============================================ */
            /* Virtual File System + Ext2                   */
            /* ============================================ */
            if (disk_ready) {
                /* Initialiser le VFS */
                vfs_init();
                
                /* Enregistrer le driver Ext2 */
                ext2_init();
                
                /* Monter le disque en tant que racine */
                if (vfs_mount("/", "ext2", NULL) == 0) {
                    /* Test: Lister le contenu de la racine */
                    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
                    console_puts("\n--- Root Directory Contents ---\n");
                    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                    
                    vfs_node_t* root = vfs_get_root();
                    if (root != NULL) {
                        uint32_t index = 0;
                        vfs_dirent_t* entry;
                        
                        while ((entry = vfs_readdir(root, index)) != NULL) {
                            /* Afficher le type */
                            if (entry->type == VFS_DIRECTORY) {
                                console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
                                console_puts("[DIR]  ");
                            } else {
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                                console_puts("[FILE] ");
                            }
                            
                            console_puts(entry->name);
                            
                            /* Afficher la taille pour les fichiers */
                            if (entry->type == VFS_FILE) {
                                vfs_node_t* file = vfs_finddir(root, entry->name);
                                if (file != NULL) {
                                    console_puts(" (");
                                    console_put_dec(file->size);
                                    console_puts(" bytes)");
                                }
                            }
                            
                            console_puts("\n");
                            index++;
                        }
                        
                        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                        console_puts("Total: ");
                        console_put_dec(index);
                        console_puts(" entries\n");
                        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                        
                        /* Test: Lire un fichier si présent */
                        vfs_node_t* test_file = vfs_open("/hello.txt", VFS_O_RDONLY);
                        if (test_file != NULL) {
                            console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
                            console_puts("\n--- Content of /hello.txt ---\n");
                            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            
                            uint8_t* buf = (uint8_t*)kmalloc(test_file->size + 1);
                            if (buf != NULL) {
                                int bytes = vfs_read(test_file, 0, test_file->size, buf);
                                if (bytes > 0) {
                                    buf[bytes] = '\0';
                                    console_puts((const char*)buf);
                                    console_puts("\n");
                                }
                                kfree(buf);
                            }
                            vfs_close(test_file);
                        }
                        
                        /* Test: Allocation de bloc et d'inode */
                        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
                        console_puts("\n--- Ext2 Allocation Test ---\n");
                        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                        
                        /* Récupérer le contexte ext2 depuis le mount */
                        vfs_mount_t* root_mount = vfs_get_root_mount();
                        if (root_mount != NULL && root_mount->fs_specific != NULL) {
                            ext2_fs_t* ext2_ctx = (ext2_fs_t*)root_mount->fs_specific;
                            
                            /* Afficher les compteurs avant allocation */
                            console_puts("Before: Free blocks=");
                            console_put_dec(ext2_ctx->superblock.s_free_blocks_count);
                            console_puts(", Free inodes=");
                            console_put_dec(ext2_ctx->superblock.s_free_inodes_count);
                            console_puts("\n");
                            
                            /* Allouer un bloc */
                            int32_t new_block = ext2_alloc_block(ext2_ctx);
                            if (new_block >= 0) {
                                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                                console_puts("Allocated block #");
                                console_put_dec((uint32_t)new_block);
                                console_puts("\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            } else {
                                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
                                console_puts("Block allocation FAILED!\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            }
                            
                            /* Allouer un inode */
                            int32_t new_inode = ext2_alloc_inode(ext2_ctx);
                            if (new_inode >= 0) {
                                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                                console_puts("Allocated inode #");
                                console_put_dec((uint32_t)new_inode);
                                console_puts("\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            } else {
                                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
                                console_puts("Inode allocation FAILED!\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            }
                            
                            /* Afficher les compteurs après allocation */
                            console_puts("After:  Free blocks=");
                            console_put_dec(ext2_ctx->superblock.s_free_blocks_count);
                            console_puts(", Free inodes=");
                            console_put_dec(ext2_ctx->superblock.s_free_inodes_count);
                            console_puts("\n");
                            
                            /* Test de libération (optionnel - décommenter pour tester) */
                            /*
                            if (new_block >= 0) {
                                ext2_free_block(ext2_ctx, (uint32_t)new_block);
                                console_puts("Freed block #");
                                console_put_dec((uint32_t)new_block);
                                console_puts("\n");
                            }
                            if (new_inode >= 0) {
                                ext2_free_inode(ext2_ctx, (uint32_t)new_inode);
                                console_puts("Freed inode #");
                                console_put_dec((uint32_t)new_inode);
                                console_puts("\n");
                            }
                            */
                        }
                        
                        /* ============================================ */
                        /* Test d'écriture dans un fichier              */
                        /* ============================================ */
                        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
                        console_puts("\n--- Ext2 Write Test ---\n");
                        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                        
                        /* Ouvrir hello.txt pour écriture */
                        vfs_node_t* write_file = vfs_open("/hello.txt", VFS_O_RDWR);
                        if (write_file != NULL) {
                            /* Lire le contenu actuel */
                            console_puts("Original content: ");
                            uint8_t* orig_buf = (uint8_t*)kmalloc(write_file->size + 1);
                            if (orig_buf) {
                                int orig_len = vfs_read(write_file, 0, write_file->size, orig_buf);
                                if (orig_len > 0) {
                                    orig_buf[orig_len] = '\0';
                                    console_puts((const char*)orig_buf);
                                }
                                kfree(orig_buf);
                            }
                            console_puts("\n");
                            
                            /* Écrire un nouveau contenu */
                            const char* new_content = "Modified by ALOS kernel!\n";
                            uint32_t new_len = 0;
                            while (new_content[new_len]) new_len++;
                            
                            int written = vfs_write(write_file, 0, new_len, (const uint8_t*)new_content);
                            if (written > 0) {
                                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                                console_puts("Wrote ");
                                console_put_dec((uint32_t)written);
                                console_puts(" bytes\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                                
                                /* Relire pour vérifier */
                                console_puts("New content: ");
                                uint8_t* verify_buf = (uint8_t*)kmalloc(write_file->size + 1);
                                if (verify_buf) {
                                    int verify_len = vfs_read(write_file, 0, write_file->size, verify_buf);
                                    if (verify_len > 0) {
                                        verify_buf[verify_len] = '\0';
                                        console_puts((const char*)verify_buf);
                                    }
                                    kfree(verify_buf);
                                }
                            } else {
                                console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
                                console_puts("Write FAILED!\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            }
                            
                            vfs_close(write_file);
                        } else {
                            console_puts("Could not open /hello.txt for writing\n");
                        }
                    }
                }
            }
            
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
                        
                        /* === Configuration IP via DHCP === */
                        NetInterface* netif = netif_get_default();
                        if (netif != NULL) {
                            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
                            console_puts("\n[NET] Starting DHCP configuration...\n");
                            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            
                            /* Initialiser et démarrer DHCP */
                            dhcp_init(netif);
                            dhcp_discover(netif);
                            
                            /* Attendre la configuration DHCP (polling avec timeout) */
                            console_puts("[NET] Waiting for DHCP response...\n");
                            for (int i = 0; i < 50 && !dhcp_is_bound(netif); i++) {
                                /* Petite pause pour laisser les interruptions traiter les paquets */
                                for (volatile int j = 0; j < 1000000; j++);
                                
                                /* Permettre les interruptions d'être traitées */
                                asm volatile("sti");
                                asm volatile("hlt");  /* Attend la prochaine interruption */
                            }
                            
                            if (dhcp_is_bound(netif)) {
                                console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
                                console_puts("[NET] DHCP configuration complete!\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                                
                                /* === Test DNS + Ping === */
                                if (netif->dns_server != 0) {
                                    dns_init(netif->dns_server);
                                    
                                    /* Test: ping google.com (résolution DNS + ICMP) */
                                    ping("google.com");
                                }
                            } else {
                                console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
                                console_puts("[NET] DHCP timeout - no response received\n");
                                console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
                            }
                        }
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