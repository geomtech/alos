/* src/kernel/kernel.c */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../arch/x86/gdt.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/io.h"
#include "../arch/x86/tss.h"
#include "../arch/x86/usermode.h"
#include "../include/multiboot.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "console.h"
#include "timer.h"
#include "klog.h"
#include "syscall.h"
#include "keymap.h"
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
#include "../net/l4/tcp.h"
#include "../shell/shell.h"
#include "../include/string.h"
#include "../mm/vmm.h"
#include "process.h"
#include "thread.h"
#include "workqueue.h"
#include "../config/config.h"

/* Variables globales pour les infos Multiboot */
static multiboot_info_t *g_mboot_info = NULL;
static uint32_t g_mboot_magic = 0;

void kernel_main(uint32_t magic, multiboot_info_t *mboot_info)
{
    /* Sauvegarder les infos Multiboot */
    g_mboot_magic = magic;
    g_mboot_info = mboot_info;

    init_gdt();
    init_idt();
    syscall_init();  /* Initialiser les syscalls (INT 0x80) */

    /* Initialiser la console avec scrolling */
    console_init();
    console_clear(VGA_COLOR_BLUE);
    
    /* Initialiser les keymaps clavier (QWERTY par défaut) */
    keymap_init();
    
    /* Initialiser le système de logs précoce (buffer mémoire) */
    klog_early_init();

    /* Vérifier le magic number */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        KLOG_ERROR("KERNEL", "Invalid Multiboot magic!");
    } else {
        KLOG_INFO_HEX("KERNEL", "Multiboot magic OK: ", magic);

        /* Infos mémoire */
        if (mboot_info->flags & MULTIBOOT_INFO_MEMORY) {
            KLOG_INFO_DEC("KERNEL", "Memory Lower (KB): ", mboot_info->mem_lower);
            KLOG_INFO_DEC("KERNEL", "Memory Upper (KB): ", mboot_info->mem_upper);
            
            uint32_t total_mb = (mboot_info->mem_lower + mboot_info->mem_upper + 1024) / 1024;
            KLOG_INFO_DEC("KERNEL", "Total RAM (MB): ~", total_mb);
        }
        
        /* Bootloader */
        if (mboot_info->flags & MULTIBOOT_INFO_BOOT_LOADER) {
            klog(LOG_INFO, "KERNEL", (const char *)mboot_info->boot_loader_name);
        }
        
        KLOG_INFO_HEX("KERNEL", "Flags: ", mboot_info->flags);

        /* ============================================ */
        /* Initialisation du Timer (PIT + RTC)         */
        /* ============================================ */
        timer_init(TIMER_FREQUENCY);  /* 1000 Hz = 1ms par tick */
        
        /* Afficher la date/heure de boot */
        console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_puts("Boot time: ");
        timestamp_print_now();
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        /* ============================================ */
        /* Initialisation du PMM                       */
        /* ============================================ */
        init_pmm(mboot_info);
        
        KLOG_INFO("PMM", "=== Physical Memory Manager ===");
        KLOG_INFO_DEC("PMM", "Free blocks: ", pmm_get_free_blocks());
        KLOG_INFO_DEC("PMM", "Free memory (KiB): ", pmm_get_free_memory() / 1024);

        /* ============================================ */
        /* Initialisation du Kernel Heap               */
        /* ============================================ */
        
        #define HEAP_PAGES 256  /* 1 MiB */
        void* heap_mem = pmm_alloc_blocks(HEAP_PAGES);
        
        if (heap_mem == NULL) {
            KLOG_ERROR("HEAP", "Failed to allocate heap memory!");
        } else {
            kheap_init(heap_mem, HEAP_PAGES * PMM_BLOCK_SIZE);
            
            KLOG_INFO("HEAP", "=== Kernel Heap (kmalloc) ===");
            KLOG_INFO_HEX("HEAP", "Heap start: ", (uint32_t)(uintptr_t)heap_mem);
            KLOG_INFO_DEC("HEAP", "Size (KiB): ", kheap_get_total_size() / 1024);
            KLOG_INFO_DEC("HEAP", "Header size (bytes): ", sizeof(KHeapBlock));
            
            /* ============================================ */
            /* Virtual Memory Manager (Paging)              */
            /* ============================================ */
            vmm_init();
            
            /* ============================================ */
            /* Scheduler & Workqueue                        */
            /* ============================================ */
            scheduler_init();
            workqueue_init();
            
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
                    /* Initialiser le système de logs fichier */
                    /* Crée /system/logs/kernel.log et vide le buffer précoce */
                    klog_init();
                    klog_flush();
                    
                    /* Initialiser le système de configuration */
                    config_init();
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
                
                /* Charger la configuration réseau depuis /config/network.conf */
                network_config_t net_config;
                int use_dhcp = 1;  /* Par défaut: DHCP */
                
                if (config_load_network(&net_config) == 0) {
                    use_dhcp = net_config.use_dhcp;
                    if (!use_dhcp) {
                        /* Appliquer la configuration statique */
                        config_apply_network(&net_config);
                        KLOG_INFO("NET", "Loaded static IP from /config/network.conf");
                    }
                }
                
                /* Démarrer la carte PCnet si c'est le driver utilisé */
                netdev_t* dev = netdev_get_default();
                if (dev != NULL && dev->type == NETDEV_TYPE_PCNET) {
                    PCNetDevice* pcnet_dev = (PCNetDevice*)dev->driver_data;
                    if (pcnet_start(pcnet_dev)) {
                        KLOG_INFO("NET", "Network stack ready!");
                        
                        NetInterface* netif = netif_get_default();
                        if (netif != NULL) {
                            if (use_dhcp) {
                                /* === Configuration IP via DHCP === */
                                console_puts("[DHCP] Starting DHCP configuration...\n");
                                
                                /* Initialiser et démarrer DHCP */
                                dhcp_init(netif);
                                dhcp_discover(netif);
                                
                                /* Attendre la configuration DHCP (polling avec timeout) */
                                console_puts("[DHCP] Waiting for DHCP response...\n");
                                for (int i = 0; i < 200 && !dhcp_is_bound(netif); i++) {
                                    /* Activer les interruptions pour recevoir les paquets */
                                    asm volatile("sti");
                                    
                                    /* Traiter les paquets réseau en attente */
                                    net_poll();
                                    
                                    /* Afficher un point tous les 20 tours */
                                    if (i % 20 == 0) {
                                        console_putc('.');
                                    }
                                    
                                    /* Petite pause */
                                    for (volatile int j = 0; j < 50000; j++);
                                }
                                console_putc('\n');
                                
                                if (dhcp_is_bound(netif)) {
                                    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
                                    console_puts("[DHCP] Configuration complete!\n");
                                    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                                } else {
                                    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
                                    console_puts("[DHCP] Configuration timed out!\n");
                                    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                                }
                            } else {
                                console_puts("[NET] Using static IP configuration\n");
                            }
                            
                            /* === Initialisation DNS === */
                            if (netif->dns_server != 0) {
                                dns_init(netif->dns_server);
                            }
                            
                            /* === Initialisation TCP === */
                            tcp_init();
                        }
                    }
                }
            }
        }
        
        /* Instructions */
        /* console_clear(VGA_COLOR_BLACK); */
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        console_puts("Welcome to ALOS - Alexy Operating System v0.1 - ");
        timestamp_print_now();
        console_puts("\n");
        console_puts("\n");
        console_puts(" * GitHub: https://github.com/geomtech/alos\n");
        console_puts(" * Type 'help' for a list of commands.\n");
        console_puts("\n");
    }

    console_refresh();
    
    asm volatile("sti");

    /* ============================================ */
    /* Initialiser le User Mode Support (TSS)       */
    /* ============================================ */
    init_usermode();

    /* ============================================ */
    /* Initialiser le Multitasking                  */
    /* ============================================ */
    init_multitasking();
    
    /* Lancer le shell interactif */
    shell_init();
    
    /* Exécuter le script de démarrage si présent */
    if (config_run_startup_script() == 0) {
        console_puts("\n");  /* Ligne vide après le script */
    }
    
    shell_run();
    
    /* Ne devrait jamais arriver */
    while (1)
    {
        asm volatile("hlt");
    }
}