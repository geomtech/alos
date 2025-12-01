/* src/drivers/pcnet.c - AMD PCnet-PCI II (Am79C970A) Driver */
#include "pcnet.h"
#include "../io.h"
#include "../kheap.h"
#include "../console.h"

/* Instance globale du driver PCnet */
static PCNetDevice* g_pcnet_dev = NULL;

/* ============================================ */
/*           PCI Helper Functions               */
/* ============================================ */

void pci_enable_bus_mastering(PCIDevice* dev)
{
    /* Lire le Command Register PCI (offset 0x04) */
    uint32_t command = pci_config_read_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    
    console_puts("[PCnet] PCI Command before: ");
    console_put_hex(command);
    console_puts("\n");
    
    /* 
     * Bits du Command Register:
     * Bit 0: I/O Space Enable
     * Bit 1: Memory Space Enable  
     * Bit 2: Bus Master Enable <- C'est celui qu'on veut !
     */
    command |= (1 << 0);  /* Enable I/O Space */
    command |= (1 << 1);  /* Enable Memory Space */
    command |= (1 << 2);  /* Enable Bus Mastering */
    
    /* Écrire le nouveau Command Register */
    pci_config_write_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND, command);
    
    /* Relire pour vérifier */
    uint32_t verify = pci_config_read_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    console_puts("[PCnet] PCI Command after: ");
    console_put_hex(verify);
    console_puts("\n");
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    console_puts("[PCnet] Bus Mastering enabled\n");
}

/* ============================================ */
/*           Low-Level I/O Functions            */
/* ============================================ */

/**
 * Réinitialise la carte PCnet et active le mode DWIO (32-bit).
 */
static void pcnet_reset(PCNetDevice* dev)
{
    /* 
     * Reset en mode WIO (16-bit).
     * Une lecture du registre RESET déclenche un software reset.
     */
    inw(dev->io_base + PCNET_RESET);
    
    /* Pause pour laisser le reset se faire */
    for (volatile int i = 0; i < 100000; i++);
}

uint32_t pcnet_read_csr(PCNetDevice* dev, uint32_t csr_no)
{
    /* Écrire le numéro du CSR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)csr_no);
    /* Lire la valeur depuis RDP (16-bit WIO) */
    return inw(dev->io_base + PCNET_RDP);
}

void pcnet_write_csr(PCNetDevice* dev, uint32_t csr_no, uint32_t value)
{
    /* Écrire le numéro du CSR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)csr_no);
    /* Écrire la valeur dans RDP (16-bit WIO) */
    outw(dev->io_base + PCNET_RDP, (uint16_t)value);
}

uint32_t pcnet_read_bcr(PCNetDevice* dev, uint32_t bcr_no)
{
    /* Écrire le numéro du BCR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)bcr_no);
    /* Lire la valeur depuis BDP (16-bit WIO) */
    return inw(dev->io_base + PCNET_BDP);
}

void pcnet_write_bcr(PCNetDevice* dev, uint32_t bcr_no, uint32_t value)
{
    /* Écrire le numéro du BCR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)bcr_no);
    /* Écrire la valeur dans BDP (16-bit WIO) */
    outw(dev->io_base + PCNET_BDP, (uint16_t)value);
}

/* ============================================ */
/*           MAC Address Reading                */
/* ============================================ */

/**
 * Lit l'adresse MAC depuis l'EEPROM/APROM.
 * L'APROM est toujours accessible octet par octet aux offsets 0x00-0x05.
 * On utilise inb() car l'APROM ne dépend pas du mode WIO/DWIO.
 */
static void pcnet_read_mac(PCNetDevice* dev)
{
    /* 
     * L'APROM contient l'adresse MAC dans les 6 premiers octets.
     * On lit octet par octet pour être sûr.
     */
    for (int i = 0; i < 6; i++) {
        dev->mac_addr[i] = inb(dev->io_base + i);
    }
}

/**
 * Affiche l'adresse MAC de manière formatée.
 */
static void pcnet_print_mac(PCNetDevice* dev)
{
    const char hex[] = "0123456789ABCDEF";
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[PCnet] MAC Address: ");
    console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
    
    for (int i = 0; i < 6; i++) {
        console_putc(hex[(dev->mac_addr[i] >> 4) & 0x0F]);
        console_putc(hex[dev->mac_addr[i] & 0x0F]);
        if (i < 5) {
            console_putc(':');
        }
    }
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

/* ============================================ */
/*           Initialization                     */
/* ============================================ */

/**
 * Vérifie et affiche l'état du CSR0.
 */
static void pcnet_print_status(PCNetDevice* dev)
{
    uint32_t csr0 = pcnet_read_csr(dev, CSR0);
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    console_puts("[PCnet] CSR0 Status: ");
    console_put_hex(csr0);
    console_puts(" (");
    
    if (csr0 & CSR0_STOP) console_puts("STOP ");
    if (csr0 & CSR0_STRT) console_puts("STRT ");
    if (csr0 & CSR0_INIT) console_puts("INIT ");
    if (csr0 & CSR0_TXON) console_puts("TXON ");
    if (csr0 & CSR0_RXON) console_puts("RXON ");
    if (csr0 & CSR0_IDON) console_puts("IDON ");
    if (csr0 & CSR0_ERR)  console_puts("ERR ");
    
    console_puts(")\n");
}

/**
 * Configure le style logiciel en mode 32-bit PCnet-PCI.
 */
static void pcnet_set_software_style(PCNetDevice* dev)
{
    /* Lire BCR20 (Software Style) */
    uint32_t bcr20 = pcnet_read_bcr(dev, BCR20);
    
    /* Forcer le style PCNET-PCI 32-bit (Style 2) */
    bcr20 = (bcr20 & ~0xFF) | SWSTYLE_PCNET_PCI;
    
    pcnet_write_bcr(dev, BCR20, bcr20);
    
    console_puts("[PCnet] Software Style set to PCNET-PCI (32-bit)\n");
}

PCNetDevice* pcnet_init(PCIDevice* pci_dev)
{
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("\n=== PCnet Driver Initialization ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    if (pci_dev == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] ERROR: No PCI device provided!\n");
        return NULL;
    }
    
    /* Allouer la structure du driver */
    PCNetDevice* dev = (PCNetDevice*)kmalloc(sizeof(PCNetDevice));
    if (dev == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] ERROR: Failed to allocate driver structure!\n");
        return NULL;
    }
    
    /* Initialiser la structure */
    dev->pci_dev = pci_dev;
    dev->io_base = pci_dev->bar0 & 0xFFFFFFFC;  /* Masquer les bits de type */
    dev->initialized = false;
    dev->packets_rx = 0;
    dev->packets_tx = 0;
    dev->errors = 0;
    dev->rx_index = 0;
    dev->tx_index = 0;
    dev->init_block = NULL;
    dev->rx_ring = NULL;
    dev->tx_ring = NULL;
    dev->rx_buffers = NULL;
    dev->tx_buffers = NULL;
    
    console_puts("[PCnet] I/O Base: ");
    console_put_hex(dev->io_base);
    console_puts("\n");
    
    /* Étape 1: Activer le Bus Mastering PCI */
    pci_enable_bus_mastering(pci_dev);
    
    /* Étape 2: Reset de la carte */
    console_puts("[PCnet] Resetting card...\n");
    pcnet_reset(dev);
    
    /* Petite pause post-reset */
    for (volatile int i = 0; i < 100000; i++);
    
    /* Étape 3: Vérifier l'état initial (CSR0) - en mode WIO 16-bit */
    pcnet_print_status(dev);
    
    /* Étape 4: Lire l'adresse MAC */
    pcnet_read_mac(dev);
    pcnet_print_mac(dev);
    
    /* Étape 5: Allouer l'Initialization Block */
    /* 
     * L'Init Block doit être aligné sur 4 octets minimum.
     * On alloue un peu plus pour s'assurer de l'alignement.
     */
    dev->init_block = (PCNetInitBlock*)kmalloc(sizeof(PCNetInitBlock) + 16);
    if (dev->init_block == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] ERROR: Failed to allocate Init Block!\n");
        kfree(dev);
        return NULL;
    }
    
    /* Aligner sur 16 octets si nécessaire */
    uint32_t init_block_addr = (uint32_t)(uintptr_t)dev->init_block;
    if (init_block_addr & 0xF) {
        init_block_addr = (init_block_addr + 15) & ~0xF;
        dev->init_block = (PCNetInitBlock*)(uintptr_t)init_block_addr;
    }
    
    console_puts("[PCnet] Init Block allocated at: ");
    console_put_hex((uint32_t)(uintptr_t)dev->init_block);
    console_puts(" (size: ");
    console_put_dec(sizeof(PCNetInitBlock));
    console_puts(" bytes)\n");
    
    /* Vérifier l'alignement */
    if ((uint32_t)(uintptr_t)dev->init_block & 0x3) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] WARNING: Init Block not 4-byte aligned!\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
        console_puts("[PCnet] Init Block alignment: OK (4-byte aligned)\n");
    }
    
    /* Stocker l'instance globale */
    g_pcnet_dev = dev;
    dev->initialized = true;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[PCnet] Driver initialized successfully!\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    return dev;
}

bool pcnet_start(PCNetDevice* dev)
{
    /* TODO: Implémenter le démarrage complet */
    (void)dev;
    return false;
}

void pcnet_stop(PCNetDevice* dev)
{
    if (dev == NULL) return;
    
    /* Écrire STOP dans CSR0 */
    pcnet_write_csr(dev, CSR0, CSR0_STOP);
    
    console_puts("[PCnet] Card stopped\n");
}

PCNetDevice* pcnet_get_device(void)
{
    return g_pcnet_dev;
}
