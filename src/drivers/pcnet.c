/* src/drivers/pcnet.c - AMD PCnet-PCI II (Am79C970A) Driver */
#include "pcnet.h"
#include "../io.h"
#include "../kheap.h"
#include "../console.h"
#include "../net/utils.h"
#include "../net/ethernet.h"

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
    /* Désactiver les interruptions pour protéger l'accès RAP/RDP */
    asm volatile("cli");
    
    /* Écrire le numéro du CSR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)csr_no);
    /* Lire la valeur depuis RDP (16-bit WIO) */
    uint32_t value = inw(dev->io_base + PCNET_RDP);
    
    /* Réactiver les interruptions */
    asm volatile("sti");
    
    return value;
}

void pcnet_write_csr(PCNetDevice* dev, uint32_t csr_no, uint32_t value)
{
    /* Désactiver les interruptions pour protéger l'accès RAP/RDP */
    asm volatile("cli");
    
    /* Écrire le numéro du CSR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)csr_no);
    /* Écrire la valeur dans RDP (16-bit WIO) */
    outw(dev->io_base + PCNET_RDP, (uint16_t)value);
    
    /* Réactiver les interruptions */
    asm volatile("sti");
}

uint32_t pcnet_read_bcr(PCNetDevice* dev, uint32_t bcr_no)
{
    /* Désactiver les interruptions pour protéger l'accès RAP/BDP */
    asm volatile("cli");
    
    /* Écrire le numéro du BCR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)bcr_no);
    /* Lire la valeur depuis BDP (16-bit WIO) */
    uint32_t value = inw(dev->io_base + PCNET_BDP);
    
    /* Réactiver les interruptions */
    asm volatile("sti");
    
    return value;
}

void pcnet_write_bcr(PCNetDevice* dev, uint32_t bcr_no, uint32_t value)
{
    /* Désactiver les interruptions pour protéger l'accès RAP/BDP */
    asm volatile("cli");
    
    /* Écrire le numéro du BCR dans RAP (16-bit WIO) */
    outw(dev->io_base + PCNET_RAP, (uint16_t)bcr_no);
    /* Écrire la valeur dans BDP (16-bit WIO) */
    outw(dev->io_base + PCNET_BDP, (uint16_t)value);
    
    /* Réactiver les interruptions */
    asm volatile("sti");
}

/* ============================================ */
/*           Packet Reception                   */
/* ============================================ */

/**
 * Traite les paquets reçus.
 * Appelé depuis le handler d'interruption quand RINT est set.
 */
static void pcnet_receive(PCNetDevice* dev)
{
    if (dev == NULL || dev->rx_ring == NULL) return;
    
    /* Traiter tous les paquets disponibles */
    while (1) {
        int idx = dev->rx_index;
        PCNetRxDesc* desc = &dev->rx_ring[idx];
        
        /* Vérifier si le descripteur appartient au CPU (OWN = 0) */
        if (desc->status & 0x8000) {
            /* Encore possédé par la carte, plus rien à lire */
            break;
        }
        
        /* Vérifier les erreurs */
        if (desc->status & 0x4000) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            console_puts("[RX] Error in packet! Status: ");
            console_put_hex(desc->status);
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        } else {
            /* Paquet valide - récupérer la taille */
            /* mcnt contient la taille du message (12 bits bas) */
            uint16_t len = desc->mcnt & 0x0FFF;
            
            /* Récupérer le pointeur vers les données */
            uint8_t* buffer = (uint8_t*)(uintptr_t)desc->rbadr;
            
            /* Log minimal de réception */
            console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
            console_puts("[RX] Packet: ");
            console_put_dec(len);
            console_puts(" bytes\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            
            /* Passer le paquet à la couche Ethernet pour traitement */
            ethernet_handle_packet(buffer, len);
        }
        
        /* CRITIQUE: Rendre le descripteur à la carte */
        desc->bcnt = 0xF000 | ((uint16_t)(-(int16_t)PCNET_BUFFER_SIZE) & 0x0FFF);
        desc->mcnt = 0;
        desc->status = 0x8000;  /* OWN = 1, carte peut réutiliser */
        
        /* Passer au descripteur suivant */
        dev->rx_index = (dev->rx_index + 1) % PCNET_RX_BUFFERS;
    }
}

/* ============================================ */
/*           Interrupt Handler                  */
/* ============================================ */

/**
 * Handler d'interruption PCnet (IRQ 11).
 * Appelé par le handler ASM irq11_handler.
 * 
 * IMPORTANT: Les interruptions PCI sont "level triggered".
 * Il faut acquitter les flags dans CSR0 AVANT d'envoyer l'EOI au PIC,
 * sinon la carte continue d'asserter l'IRQ et on boucle infiniment.
 */
void pcnet_irq_handler(void)
{
    if (g_pcnet_dev == NULL) return;
    
    /* Lire CSR0 pour voir ce qui a déclenché l'interruption */
    uint32_t csr0 = pcnet_read_csr(g_pcnet_dev, CSR0);
    
    /* 
     * Acquitter les interruptions en écrivant 1 sur les bits d'interruption.
     * Les bits 8-15 sont "write-1-to-clear" (écrire 1 efface le flag).
     * On doit garder IENA (bit 6) actif pour continuer à recevoir les IRQ.
     * 
     * Bits à effacer (écrire 1):
     * - IDON (bit 8): Initialization Done
     * - TINT (bit 9): Transmit Interrupt
     * - RINT (bit 10): Receive Interrupt
     * - MERR (bit 11): Memory Error
     * - MISS (bit 12): Missed Frame
     * - CERR (bit 13): Collision Error
     * - BABL (bit 14): Babble
     * - ERR (bit 15): Error summary
     * 
     * On écrit: (csr0 & 0xFF00) | IENA
     * Cela efface tous les flags actifs tout en gardant IENA.
     */
    uint32_t ack = (csr0 & 0xFF00) | CSR0_IENA;
    pcnet_write_csr(g_pcnet_dev, CSR0, ack);
    
    /* Traiter les paquets reçus si RINT */
    if (csr0 & CSR0_RINT) {
        pcnet_receive(g_pcnet_dev);
        g_pcnet_dev->packets_rx++;
    }
    
    /* Mettre à jour les statistiques TX */
    if (csr0 & CSR0_TINT) {
        g_pcnet_dev->packets_tx++;
    }
    
    if (csr0 & CSR0_ERR) {
        g_pcnet_dev->errors++;
    }
    
    if (csr0 & CSR0_IDON) {
        g_pcnet_dev->initialized = true;
    }
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
    console_puts("[PCnet] BCR20 before: ");
    console_put_hex(bcr20);
    
    /* Forcer le style PCNET-PCI 32-bit (Style 2) */
    bcr20 = (bcr20 & ~0xFF) | SWSTYLE_PCNET_PCI;
    
    pcnet_write_bcr(dev, BCR20, bcr20);
    
    /* Vérifier que ça a pris */
    bcr20 = pcnet_read_bcr(dev, BCR20);
    console_puts(" -> after: ");
    console_put_hex(bcr20);
    console_puts("\n");
    
    if ((bcr20 & 0xFF) == SWSTYLE_PCNET_PCI) {
        console_puts("[PCnet] Software Style set to PCNET-PCI (32-bit descriptors)\n");
    } else {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] WARNING: Failed to set SWSTYLE!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }
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
    
    /* Étape 2b: Configurer SWSTYLE = 2 (32-bit PCnet-PCI) AVANT toute allocation */
    pcnet_set_software_style(dev);
    
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
    
    /* Étape 6: Allouer les Descriptor Rings (alignés sur 16 octets) */
    dev->rx_ring = (PCNetRxDesc*)kmalloc(sizeof(PCNetRxDesc) * PCNET_RX_BUFFERS + 16);
    dev->tx_ring = (PCNetTxDesc*)kmalloc(sizeof(PCNetTxDesc) * PCNET_TX_BUFFERS + 16);
    
    if (dev->rx_ring == NULL || dev->tx_ring == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] ERROR: Failed to allocate descriptor rings!\n");
        return NULL;
    }
    
    /* Aligner les rings sur 16 octets */
    uint32_t rx_ring_addr = (uint32_t)(uintptr_t)dev->rx_ring;
    if (rx_ring_addr & 0xF) {
        rx_ring_addr = (rx_ring_addr + 15) & ~0xF;
        dev->rx_ring = (PCNetRxDesc*)(uintptr_t)rx_ring_addr;
    }
    
    uint32_t tx_ring_addr = (uint32_t)(uintptr_t)dev->tx_ring;
    if (tx_ring_addr & 0xF) {
        tx_ring_addr = (tx_ring_addr + 15) & ~0xF;
        dev->tx_ring = (PCNetTxDesc*)(uintptr_t)tx_ring_addr;
    }
    
    console_puts("[PCnet] RX Ring at: ");
    console_put_hex((uint32_t)(uintptr_t)dev->rx_ring);
    console_puts(", TX Ring at: ");
    console_put_hex((uint32_t)(uintptr_t)dev->tx_ring);
    console_puts("\n");
    
    /* Étape 7: Allouer les buffers de données */
    dev->rx_buffers = (uint8_t*)kmalloc(PCNET_BUFFER_SIZE * PCNET_RX_BUFFERS);
    dev->tx_buffers = (uint8_t*)kmalloc(PCNET_BUFFER_SIZE * PCNET_TX_BUFFERS);
    
    if (dev->rx_buffers == NULL || dev->tx_buffers == NULL) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] ERROR: Failed to allocate data buffers!\n");
        return NULL;
    }
    
    console_puts("[PCnet] RX Buffers at: ");
    console_put_hex((uint32_t)(uintptr_t)dev->rx_buffers);
    console_puts(", TX Buffers at: ");
    console_put_hex((uint32_t)(uintptr_t)dev->tx_buffers);
    console_puts("\n");
    
    /* Étape 8: Initialiser les descripteurs RX */
    for (int i = 0; i < PCNET_RX_BUFFERS; i++) {
        dev->rx_ring[i].rbadr = (uint32_t)(uintptr_t)(dev->rx_buffers + i * PCNET_BUFFER_SIZE);
        dev->rx_ring[i].bcnt = (int16_t)(-(int16_t)PCNET_BUFFER_SIZE);  /* Complément à 2 */
        dev->rx_ring[i].status = 0x8000;  /* OWN = 1 (Card owned, prêt à recevoir) */
        dev->rx_ring[i].mcnt = 0;
        dev->rx_ring[i].user = 0;
    }
    
    /* Étape 9: Initialiser les descripteurs TX */
    for (int i = 0; i < PCNET_TX_BUFFERS; i++) {
        dev->tx_ring[i].tbadr = (uint32_t)(uintptr_t)(dev->tx_buffers + i * PCNET_BUFFER_SIZE);
        dev->tx_ring[i].bcnt = 0;
        dev->tx_ring[i].status = 0;  /* OWN = 0 (CPU owned) */
        dev->tx_ring[i].misc = 0;
        dev->tx_ring[i].user = 0;
    }
    
    console_puts("[PCnet] Descriptors initialized (");
    console_put_dec(PCNET_RX_BUFFERS);
    console_puts(" RX, ");
    console_put_dec(PCNET_TX_BUFFERS);
    console_puts(" TX)\n");
    
    /* Étape 10: Configurer l'Initialization Block */
    dev->init_block->mode = 0;  /* Normal operation */
    dev->init_block->rlen = (PCNET_LOG2_RX_BUFFERS << 4);  /* log2(16) = 4, shifted */
    dev->init_block->tlen = (PCNET_LOG2_TX_BUFFERS << 4);  /* log2(16) = 4, shifted */
    
    /* Copier l'adresse MAC */
    for (int i = 0; i < 6; i++) {
        dev->init_block->padr[i] = dev->mac_addr[i];
    }
    
    dev->init_block->reserved = 0;
    
    /* Filtre multicast (accepter tout pour l'instant) */
    for (int i = 0; i < 8; i++) {
        dev->init_block->ladr[i] = 0xFF;
    }
    
    /* Adresses des rings */
    dev->init_block->rdra = (uint32_t)(uintptr_t)dev->rx_ring;
    dev->init_block->tdra = (uint32_t)(uintptr_t)dev->tx_ring;
    
    console_puts("[PCnet] Init Block configured\n");
    
    /* Stocker l'instance globale */
    g_pcnet_dev = dev;
    dev->initialized = false;  /* Sera mis à true par pcnet_start */
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[PCnet] Driver initialized successfully!\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    return dev;
}

bool pcnet_start(PCNetDevice* dev)
{
    if (dev == NULL) return false;
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    console_puts("[PCnet] Starting card...\n");
    
    /* Étape 1: Écrire l'adresse de l'Init Block dans CSR1 et CSR2 */
    uint32_t init_addr = (uint32_t)(uintptr_t)dev->init_block;
    
    pcnet_write_csr(dev, CSR1, init_addr & 0xFFFF);         /* 16 bits bas */
    pcnet_write_csr(dev, CSR2, (init_addr >> 16) & 0xFFFF); /* 16 bits hauts */
    
    console_puts("[PCnet] Init Block address written to CSR1/CSR2: ");
    console_put_hex(init_addr);
    console_puts("\n");
    
    /* Étape 2: Lancer l'initialisation SANS interruptions (juste INIT) */
    pcnet_write_csr(dev, CSR0, CSR0_INIT);
    
    console_puts("[PCnet] Waiting for IDON...\n");
    
    /* Étape 3: Attendre IDON (Initialization Done) par polling */
    int timeout = 100000;
    uint32_t csr0;
    while (timeout > 0) {
        csr0 = pcnet_read_csr(dev, CSR0);
        if (csr0 & CSR0_IDON) {
            break;
        }
        timeout--;
    }
    
    if (timeout == 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[PCnet] ERROR: Timeout waiting for IDON!\n");
        console_puts("[PCnet] CSR0 = ");
        console_put_hex(csr0);
        console_puts("\n");
        return false;
    }
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[PCnet] IDON received! CSR0 = ");
    console_put_hex(csr0);
    console_puts("\n");
    
    /* Acquitter IDON en écrivant 1 */
    pcnet_write_csr(dev, CSR0, CSR0_IDON);
    
    /* Étape 4: Démarrer la carte (STRT + IENA pour activer les interruptions) */
    pcnet_write_csr(dev, CSR0, CSR0_STRT | CSR0_IENA);
    
    /* Vérifier que la carte est démarrée */
    csr0 = pcnet_read_csr(dev, CSR0);
    
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    console_puts("[PCnet] After START, CSR0 = ");
    console_put_hex(csr0);
    console_puts(" (");
    if (csr0 & CSR0_TXON) console_puts("TXON ");
    if (csr0 & CSR0_RXON) console_puts("RXON ");
    if (csr0 & CSR0_IENA) console_puts("IENA ");
    if (csr0 & CSR0_STRT) console_puts("STRT ");
    console_puts(")\n");
    
    dev->initialized = true;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("\n*** PCnet Started! Ready to send/receive packets ***\n\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    return true;
}

/**
 * Envoie un paquet Ethernet.
 */
bool pcnet_send(PCNetDevice* dev, const uint8_t* data, uint16_t len)
{
    if (dev == NULL || data == NULL || len == 0) return false;
    if (len > PCNET_BUFFER_SIZE) return false;
    
    /* Obtenir le descripteur TX actuel */
    int idx = dev->tx_index;
    PCNetTxDesc* desc = &dev->tx_ring[idx];
    
    /* Vérifier que le descripteur est libre (OWN = 0) */
    if (desc->status & 0x8000) {
        /* Descripteur encore possédé par la carte */
        console_puts("[PCnet] TX buffer busy!\n");
        return false;
    }
    
    /* Copier les données dans le buffer */
    uint8_t* buf = dev->tx_buffers + idx * PCNET_BUFFER_SIZE;
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = data[i];
    }
    
    /* Configurer le descripteur */
    desc->tbadr = (uint32_t)(uintptr_t)buf;
    
    /* BCNT: 12 bits, complément à 2, bits 15-12 doivent être 1 (0xF000) */
    desc->bcnt = 0xF000 | ((uint16_t)(-(int16_t)len) & 0x0FFF);
    desc->misc = 0;
    
    /* 
     * Status: OWN=1 (card owns), STP=1 (start of packet), ENP=1 (end of packet)
     * 0x8300 = OWN | STP | ENP
     */
    desc->status = 0x8300;
    
    /* Debug: afficher l'état du descripteur */
    console_puts("[TX] idx=");
    console_put_dec(idx);
    console_puts(" buf=");
    console_put_hex((uint32_t)(uintptr_t)buf);
    console_puts(" len=");
    console_put_dec(len);
    console_puts(" bcnt=");
    console_put_hex(desc->bcnt);
    console_puts(" status=");
    console_put_hex(desc->status);
    console_puts("\n");
    
    /* Passer au descripteur suivant */
    dev->tx_index = (dev->tx_index + 1) % PCNET_TX_BUFFERS;
    
    /* Déclencher l'envoi immédiat avec TDMD + garder IENA actif */
    pcnet_write_csr(dev, CSR0, CSR0_TDMD | CSR0_IENA);
    
    /* Attendre un peu et vérifier le status */
    for (volatile int i = 0; i < 100000; i++);
    
    uint16_t csr0_after = pcnet_read_csr(dev, CSR0);
    console_puts("[TX] CSR0 after=");
    console_put_hex(csr0_after);
    console_puts(" desc->status=");
    console_put_hex(desc->status);
    if (!(desc->status & 0x8000)) {
        console_puts(" (OWN cleared = sent!)");
    }
    console_puts("\n");
    
    return true;
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

void pcnet_get_mac(uint8_t* buf)
{
    if (buf == NULL) return;
    
    if (g_pcnet_dev != NULL) {
        for (int i = 0; i < 6; i++) {
            buf[i] = g_pcnet_dev->mac_addr[i];
        }
    } else {
        /* Pas de device, remplir avec des zéros */
        for (int i = 0; i < 6; i++) {
            buf[i] = 0;
        }
    }
}
