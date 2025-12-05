/* src/kernel/kernel.c - ALOS Kernel for x86-64 with Limine */
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/usermode.h"
#include "../config/config.h"
#include "../drivers/ata.h"
#include "../drivers/net/pcnet.h"
#include "../drivers/net/e1000e.h"
#include "../drivers/pci.h"
#include "mmio/mmio.h"
#include "../fs/ext2.h"
#include "../fs/vfs.h"
#include "../include/limine.h"
#include "../include/string.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../net/core/net.h"
#include "../net/core/netdev.h"
#include "../net/l3/icmp.h"
#include "../net/l3/route.h"
#include "../net/l4/dhcp.h"
#include "../net/l4/dns.h"
#include "../net/l4/tcp.h"
#include "../shell/shell.h"
#include "../gui/gui.h"
#include "console.h"
#include "fb_console.h"
#include "mouse.h"
#include "keymap.h"
#include "klog.h"
#include "process.h"
#include "syscall.h"
#include "timer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================
 * Limine Requests
 * ============================================
 * These are placed in a special section that Limine scans.
 */

/* Request markers */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

/* Base revision - required */
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(3);

/* Memory map request */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

/* HHDM (Higher Half Direct Map) request */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

/* Framebuffer request (for VGA-like output) */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

/* Kernel address request */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kernel_addr_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

/* Bootloader info request */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0
};

/* Global pointers to Limine responses */
static struct limine_memmap_response *g_memmap = NULL;
static struct limine_hhdm_response *g_hhdm = NULL;
static struct limine_framebuffer_response *g_framebuffer = NULL;
static uint64_t g_hhdm_offset = 0;

/* Fonction externe pour incrémenter les ticks */
extern void timer_tick(void);

/* Déclaration de schedule() pour le multitasking */
extern void schedule(void);

/**
 * Clear all debug registers to prevent spurious Debug exceptions.
 * This disables hardware breakpoints and clears the debug status.
 */
static void clear_debug_registers(void) {
    __asm__ volatile(
        "xor %%rax, %%rax\n"
        "mov %%rax, %%dr0\n"
        "mov %%rax, %%dr1\n"
        "mov %%rax, %%dr2\n"
        "mov %%rax, %%dr3\n"
        "mov %%rax, %%dr6\n"       /* Clear debug status */
        "mov $0x400, %%rax\n"      /* DR7 = 0x400 (breakpoints disabled, bit 10 = 1 reserved) */
        "mov %%rax, %%dr7\n"
        : : : "rax"
    );
}

/* Compteur pour le scheduling (tous les X ticks) */
static uint32_t schedule_counter = 0;
#define SCHEDULE_INTERVAL 2 /* Scheduler toutes les 2 ticks (~20ms à 100Hz) */

/* Déclaration externe pour réveiller les threads en sleep */
extern void scheduler_wake_sleeping(void);

void timer_handler_c(void) {
  /* Incrémenter le compteur de ticks */
  timer_tick();

  /* NOTE: EOI est envoyé par irq_handler() après le retour de cette fonction.
   * Ne pas envoyer de double EOI ici ! */
}

/**
 * Halt and catch fire - called on unrecoverable errors.
 */
static void hcf(void) {
    cli();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/**
 * Kernel main entry point - called by Limine.
 * Limine has already set up:
 * - Long mode (64-bit)
 * - Paging with identity map + higher half
 * - A valid stack
 */
void kmain(void) {
  /* Verify Limine base revision */
  if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
    hcf();
  }
  
  /* Get Limine responses */
  g_memmap = memmap_request.response;
  g_hhdm = hhdm_request.response;
  g_framebuffer = framebuffer_request.response;
  
  if (g_hhdm != NULL) {
    g_hhdm_offset = g_hhdm->offset;
  }

  /* Initialize framebuffer console (preferred) or VGA text mode */
  if (g_framebuffer != NULL && g_framebuffer->framebuffer_count > 0) {
    struct limine_framebuffer *fb = g_framebuffer->framebuffers[0];
    console_init_fb(fb);
  } else {
    /* Fallback to VGA text mode */
    console_set_hhdm_offset(g_hhdm_offset);
  }

  /* Initialize GDT (64-bit) */
  gdt_init();
  
  /* Initialize IDT (64-bit) */
  idt_init();
  
  /* Clear debug registers to prevent spurious Debug exceptions (INT 0x01) */
  clear_debug_registers();
  
  /* Initialize CPU features */
  cpu_init();
  
  /* Initialize syscalls (INT 0x80 + SYSCALL instruction) */
  syscall_init();

  /* Initialiser la console avec scrolling */
  console_init();

  /* Initialiser les keymaps clavier (QWERTY par défaut) */
  keymap_init();

  /* Initialiser le système de logs précoce (buffer mémoire) */
  klog_early_init();

  /* Log bootloader info */
  if (bootloader_info_request.response != NULL) {
    KLOG_INFO("KERNEL", "Booted by Limine");
    klog(LOG_INFO, "KERNEL", bootloader_info_request.response->name);
  }
  
  /* Log HHDM offset */
  if (g_hhdm != NULL) {
    KLOG_INFO_HEX("KERNEL", "HHDM offset: ", (uint32_t)(g_hhdm_offset >> 32));
    KLOG_INFO_HEX("KERNEL", "HHDM offset (low): ", (uint32_t)g_hhdm_offset);
  }
  
  /* Log memory map */
  if (g_memmap != NULL) {
    KLOG_INFO_DEC("KERNEL", "Memory map entries: ", (uint32_t)g_memmap->entry_count);
    
    uint64_t total_usable = 0;
    for (uint64_t i = 0; i < g_memmap->entry_count; i++) {
      struct limine_memmap_entry *entry = g_memmap->entries[i];
      if (entry->type == LIMINE_MEMMAP_USABLE) {
        total_usable += entry->length;
      }
    }
    KLOG_INFO_DEC("KERNEL", "Total usable RAM (MB): ", (uint32_t)(total_usable / (1024 * 1024)));
  }

    /* ============================================ */
    /* Initialisation du Timer (PIT + RTC)         */
    /* ============================================ */
    KLOG_INFO("KERNEL", "Initializing timer...");
    timer_init(TIMER_FREQUENCY); /* 1000 Hz = 1ms par tick */
    KLOG_INFO("KERNEL", "Timer initialized");

    /* Afficher la date/heure de boot */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts("Boot time: ");
    timestamp_print_now();
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* ============================================ */
    /* Initialisation du PMM                       */
    /* ============================================ */
    init_pmm_limine(g_memmap, g_hhdm_offset);

    KLOG_INFO("PMM", "=== Physical Memory Manager ===");
    KLOG_INFO_DEC("PMM", "Free blocks: ", pmm_get_free_blocks());
    KLOG_INFO_DEC("PMM", "Free memory (KiB): ", pmm_get_free_memory() / 1024);

    /* ============================================ */
    /* Initialisation du Kernel Heap               */
    /* ============================================ */

#define HEAP_PAGES 256 /* 1 MiB */
    void *heap_mem = pmm_alloc_blocks(HEAP_PAGES);

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
      /* MMIO Subsystem                               */
      /* ============================================ */
      mmio_init();

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
        /* Initialiser la couche réseau avec la MAC du périphérique par défaut
         */
        uint8_t mac[6];
        netdev_get_mac(mac);
        net_init(mac);

        /* Initialiser la table de routage */
        route_init();

        /* Charger la configuration réseau depuis /config/network.conf */
        network_config_t net_config;
        int use_dhcp = 1; /* Par défaut: DHCP */

        if (config_load_network(&net_config) == 0) {
          use_dhcp = net_config.use_dhcp;
          if (!use_dhcp) {
            /* Appliquer la configuration statique */
            config_apply_network(&net_config);
            KLOG_INFO("NET", "Loaded static IP from /config/network.conf");
          }
        }

        /* Démarrer la carte réseau (si nécessaire) */
        netdev_t *dev = netdev_get_default();
        bool net_ready = false;

        if (dev != NULL) {
          if (dev->type == NETDEV_TYPE_PCNET) {
            PCNetDevice *pcnet_dev = (PCNetDevice *)dev->driver_data;
            if (pcnet_start(pcnet_dev)) {
              net_ready = true;
            }
          } else if (dev->type == NETDEV_TYPE_E1000) {
            E1000Device *e1000_dev = (E1000Device *)dev->driver_data;
            if (e1000e_start(e1000_dev)) {
              net_ready = true;
            }
          } else if (dev->type == NETDEV_TYPE_VIRTIO) {
            /* Virtio est déjà démarré dans l'init, mais on vérifie */
            net_ready = true;
          }
        }

        if (net_ready) {
          KLOG_INFO("NET", "Network stack ready!");

          NetInterface *netif = netif_get_default();
          if (netif != NULL) {
            if (use_dhcp) {
              /* === Configuration IP via DHCP === */
              KLOG_INFO("DHCP", "Starting DHCP configuration...");

              /* Initialiser et démarrer DHCP */
              dhcp_init(netif);
              dhcp_discover(netif);

              /* Attendre la configuration DHCP (polling avec timeout) */
              KLOG_INFO("DHCP", "Waiting for DHCP response...\n");

              for (int i = 0; i < 200 && !dhcp_is_bound(netif); i++) {
                /* Polling explicite pour être robuste si les IRQ échouent */
                net_poll();

                /* Petite pause */
                for (volatile int j = 0; j < 100000; j++)
                  ;

                /* Permettre les interruptions */
                asm volatile("sti");
                asm volatile("hlt");
              }
              if (dhcp_is_bound(netif)) {
                KLOG_INFO("DHCP", "DHCP configuration complete!");
              } else {
                KLOG_WARN("DHCP", "DHCP configuration timed out");
              }
            } else {
              KLOG_INFO("NET", "Using static IP configuration");
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

    /* ============================================ */
    /* PS/2 Mouse Driver                            */
    /* ============================================ */
    if (mouse_init() == 0) {
      KLOG_INFO("MOUSE", "PS/2 mouse driver initialized");
    } else {
      KLOG_ERROR("MOUSE", "Failed to initialize PS/2 mouse");
    }

    /* Instructions */
    /* console_clear(VGA_COLOR_BLACK); disabled for debugging */
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("Welcome to ALOS - Alexy Operating System v0.1 - ");
    timestamp_print_now();
    console_puts("\n");
    console_puts("\n");
    console_puts(" * GitHub: https://github.com/geomtech/alos\n");
    console_puts(" * Type 'help' for a list of commands.\n");
    console_puts("\n");

    console_refresh();

    __asm__ volatile("sti");

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
    KLOG_INFO("STARTUP", "Startup script executed successfully");
  }

  shell_run();

  /* Ne devrait jamais arriver */
  hcf();
}

/* ============================================
 * GUI Support
 * ============================================ */

/**
 * Démarre l'interface graphique.
 * Appelé par la commande shell 'gui' ou au démarrage si configuré.
 * 
 * @return 0 en cas de succès, -1 en cas d'erreur
 */
int start_gui(void) {
    if (!g_framebuffer || g_framebuffer->framebuffer_count == 0) {
        console_puts("Error: No framebuffer available\n");
        return -1;
    }
    
    struct limine_framebuffer* fb = g_framebuffer->framebuffers[0];
    if (!fb) {
        console_puts("Error: Framebuffer is NULL\n");
        return -1;
    }
    
    console_puts("Starting ALOS GUI...\n");
    console_puts("  Resolution: ");
    console_put_dec((uint32_t)fb->width);
    console_puts("x");
    console_put_dec((uint32_t)fb->height);
    console_puts(", ");
    console_put_dec((uint32_t)fb->bpp);
    console_puts(" bpp\n");
    
    /* Désactive la console framebuffer AVANT d'initialiser le GUI */
    fb_console_set_enabled(false);
    
    /* Initialise le GUI */
    if (gui_init(fb) != 0) {
        fb_console_set_enabled(true);  /* Réactive en cas d'erreur */
        console_puts("Error: Failed to initialize GUI\n");
        return -1;
    }
    
    /* Configure les limites de la souris et l'initialise */
    mouse_set_bounds((uint32_t)fb->width, (uint32_t)fb->height);
    mouse_set_position((int32_t)fb->width / 2, (int32_t)fb->height / 2);
    
    /* Enregistre le callback pour les événements souris */
    mouse_set_callback(gui_mouse_callback);
    
    /* Configure les menus et le dock de démonstration */
    gui_setup_demo_menus();
    gui_setup_demo_dock();
    
    /* Crée une fenêtre de démonstration */
    gui_create_demo_window("Bienvenue", 150, 100);
    
    /* Effectue le rendu complet initial */
    gui_render_full();
    
    /* Note: la console est maintenant désactivée, ces messages ne s'afficheront pas
     * sur le framebuffer mais pourraient aller vers un port série si configuré */
    
    return 0;
}

/* ============================================
 * Helper functions for Limine integration
 * ============================================ */

/**
 * Get the HHDM offset for physical to virtual address conversion.
 */
uint64_t get_hhdm_offset(void) {
    return g_hhdm_offset;
}

/**
 * Convert physical address to virtual address using HHDM.
 */
void* phys_to_virt(uint64_t phys) {
    return (void*)(phys + g_hhdm_offset);
}

/**
 * Convert virtual address to physical address.
 */
uint64_t virt_to_phys(void* virt) {
    return (uint64_t)virt - g_hhdm_offset;
}