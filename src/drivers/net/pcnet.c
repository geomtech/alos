/* src/drivers/net/pcnet.c - AMD PCnet-PCI II (Am79C970A) Driver
 *
 * Ce driver supporte deux modes d'accès:
 * - PIO (Port I/O) : Mode legacy, toujours disponible
 * - MMIO (Memory-Mapped I/O) : Mode moderne, utilisé si BAR1 est MMIO
 *
 * Le mode MMIO offre de meilleures performances car il peut utiliser
 * n'importe quel registre général et bénéficie du pipelining CPU.
 */
#include "pcnet.h"
#include "../../arch/x86/idt.h"
#include "../../arch/x86/io.h"
#include "../../kernel/mmio/mmio.h"
#include "../../kernel/mmio/pci_mmio.h"
#include "../../mm/kheap.h"
#include "../../net/core/netdev.h"
#include "../../net/l2/ethernet.h"
#include "../../net/utils.h"
#include "../../kernel/klog.h"

/* Instance globale du driver PCnet */
static PCNetDevice *g_pcnet_dev = NULL;

/* NetInterface pour la nouvelle API */
static NetInterface *g_pcnet_netif = NULL;

/* Mode d'accès forcé (-1 = auto, 0 = PIO, 1 = MMIO) */
static int g_forced_access_mode = -1;

/* ============================================ */
/*           Fonctions d'accès aux registres     */
/* ============================================ */

/* pci_enable_bus_mastering moved to pci.c */

/* ============================================ */
/*           Fonctions PIO (Port I/O)           */
/* ============================================ */

/**
 * Reset via PIO (mode WIO 16-bit).
 */
static void pcnet_reset_pio(PCNetDevice *dev) {
  inw(dev->io_base + PCNET_RESET);
  for (volatile int i = 0; i < 100000; i++)
    ;
}

/**
 * Lit un CSR via PIO.
 */
static uint32_t pcnet_read_csr_pio(PCNetDevice *dev, uint32_t csr_no) {
  asm volatile("cli");
  outw(dev->io_base + PCNET_RAP, (uint16_t)csr_no);
  uint32_t value = inw(dev->io_base + PCNET_RDP);
  asm volatile("sti");
  return value;
}

/**
 * Écrit un CSR via PIO.
 */
static void pcnet_write_csr_pio(PCNetDevice *dev, uint32_t csr_no, uint32_t value) {
  asm volatile("cli");
  outw(dev->io_base + PCNET_RAP, (uint16_t)csr_no);
  outw(dev->io_base + PCNET_RDP, (uint16_t)value);
  asm volatile("sti");
}

/**
 * Lit un BCR via PIO.
 */
static uint32_t pcnet_read_bcr_pio(PCNetDevice *dev, uint32_t bcr_no) {
  asm volatile("cli");
  outw(dev->io_base + PCNET_RAP, (uint16_t)bcr_no);
  uint32_t value = inw(dev->io_base + PCNET_BDP);
  asm volatile("sti");
  return value;
}

/**
 * Écrit un BCR via PIO.
 */
static void pcnet_write_bcr_pio(PCNetDevice *dev, uint32_t bcr_no, uint32_t value) {
  asm volatile("cli");
  outw(dev->io_base + PCNET_RAP, (uint16_t)bcr_no);
  outw(dev->io_base + PCNET_BDP, (uint16_t)value);
  asm volatile("sti");
}

/* ============================================ */
/*           Fonctions MMIO                     */
/* ============================================ */

/**
 * Reset via MMIO (mode DWIO 32-bit).
 */
static void pcnet_reset_mmio(PCNetDevice *dev) {
  /* Une lecture du registre RESET déclenche un software reset */
  mmio_read32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RESET));
  /* Barrier pour s'assurer que le reset est effectué */
  mmio_mb();
  for (volatile int i = 0; i < 100000; i++)
    ;
}

/**
 * Lit un CSR via MMIO.
 * Utilise le mode DWIO 32-bit pour de meilleures performances.
 */
static uint32_t pcnet_read_csr_mmio(PCNetDevice *dev, uint32_t csr_no) {
  asm volatile("cli");
  /* Écrire le numéro du CSR dans RAP (32-bit) */
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RAP), csr_no);
  /* Barrier pour garantir l'ordre */
  mmio_wmb();
  /* Lire la valeur depuis RDP (32-bit) */
  uint32_t value = mmio_read32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RDP));
  asm volatile("sti");
  return value;
}

/**
 * Écrit un CSR via MMIO.
 */
static void pcnet_write_csr_mmio(PCNetDevice *dev, uint32_t csr_no, uint32_t value) {
  asm volatile("cli");
  /* Écrire le numéro du CSR dans RAP */
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RAP), csr_no);
  mmio_wmb();
  /* Écrire la valeur dans RDP */
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RDP), value);
  /* Barrier pour garantir que l'écriture est visible */
  mmiowb();
  asm volatile("sti");
}

/**
 * Lit un BCR via MMIO.
 */
static uint32_t pcnet_read_bcr_mmio(PCNetDevice *dev, uint32_t bcr_no) {
  asm volatile("cli");
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RAP), bcr_no);
  mmio_wmb();
  uint32_t value = mmio_read32(MMIO_REG(dev->mmio_base, PCNET_MMIO_BDP));
  asm volatile("sti");
  return value;
}

/**
 * Écrit un BCR via MMIO.
 */
static void pcnet_write_bcr_mmio(PCNetDevice *dev, uint32_t bcr_no, uint32_t value) {
  asm volatile("cli");
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RAP), bcr_no);
  mmio_wmb();
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_BDP), value);
  mmiowb();
  asm volatile("sti");
}

/* ============================================ */
/*           Fonctions publiques (dispatch)     */
/* ============================================ */

/**
 * Réinitialise la carte PCnet.
 * Sélectionne automatiquement PIO ou MMIO selon le mode configuré.
 */
static void pcnet_reset(PCNetDevice *dev) {
  if (dev->access_mode == PCNET_ACCESS_MMIO && dev->mmio_base != NULL) {
    pcnet_reset_mmio(dev);
  } else {
    pcnet_reset_pio(dev);
  }
}

uint32_t pcnet_read_csr(PCNetDevice *dev, uint32_t csr_no) {
  if (dev->access_mode == PCNET_ACCESS_MMIO && dev->mmio_base != NULL) {
    return pcnet_read_csr_mmio(dev, csr_no);
  } else {
    return pcnet_read_csr_pio(dev, csr_no);
  }
}

void pcnet_write_csr(PCNetDevice *dev, uint32_t csr_no, uint32_t value) {
  if (dev->access_mode == PCNET_ACCESS_MMIO && dev->mmio_base != NULL) {
    pcnet_write_csr_mmio(dev, csr_no, value);
  } else {
    pcnet_write_csr_pio(dev, csr_no, value);
  }
}

uint32_t pcnet_read_bcr(PCNetDevice *dev, uint32_t bcr_no) {
  if (dev->access_mode == PCNET_ACCESS_MMIO && dev->mmio_base != NULL) {
    return pcnet_read_bcr_mmio(dev, bcr_no);
  } else {
    return pcnet_read_bcr_pio(dev, bcr_no);
  }
}

void pcnet_write_bcr(PCNetDevice *dev, uint32_t bcr_no, uint32_t value) {
  if (dev->access_mode == PCNET_ACCESS_MMIO && dev->mmio_base != NULL) {
    pcnet_write_bcr_mmio(dev, bcr_no, value);
  } else {
    pcnet_write_bcr_pio(dev, bcr_no, value);
  }
}

bool pcnet_is_mmio(PCNetDevice *dev) {
  return dev != NULL && dev->access_mode == PCNET_ACCESS_MMIO;
}

void pcnet_force_access_mode(pcnet_access_mode_t mode) {
  g_forced_access_mode = (int)mode;
}

/* ============================================ */
/*           Packet Reception                   */
/* ============================================ */

/**
 * Traite les paquets reçus.
 * Appelé depuis le handler d'interruption quand RINT est set.
 */
static void pcnet_receive(PCNetDevice *dev) {
  if (dev == NULL || dev->rx_ring == NULL)
    return;

  /* Traiter tous les paquets disponibles */
  while (1) {
    int idx = dev->rx_index;
    PCNetRxDesc *desc = &dev->rx_ring[idx];

    /* Vérifier si le descripteur appartient au CPU (OWN = 0) */
    if (desc->status & 0x8000) {
      /* Encore possédé par la carte, plus rien à lire */
      break;
    }

    /* Vérifier les erreurs */
    if (desc->status & 0x4000) {
      KLOG_ERROR_HEX("PCNET", "RX Error, Status: ", desc->status);
    } else {
      /* Paquet valide - récupérer la taille */
      /* mcnt contient la taille du message (12 bits bas) */
      uint16_t len = desc->mcnt & 0x0FFF;

      /* Récupérer le pointeur vers les données */
      uint8_t *buffer = (uint8_t *)(uintptr_t)desc->rbadr;

      /* Passer le paquet à la couche Ethernet pour traitement (pas de log) */
      ethernet_handle_packet(buffer, len);
    }

    /* CRITIQUE: Rendre le descripteur à la carte */
    desc->bcnt = 0xF000 | ((uint16_t)(-(int16_t)PCNET_BUFFER_SIZE) & 0x0FFF);
    desc->mcnt = 0;
    desc->status = 0x8000; /* OWN = 1, carte peut réutiliser */

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
void pcnet_irq_handler(void) {
  if (g_pcnet_dev == NULL)
    return;

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

void pcnet_poll(void) { pcnet_irq_handler(); }

/* ============================================ */
/*           MAC Address Reading                */
/* ============================================ */

/**
 * Lit l'adresse MAC depuis l'EEPROM/APROM.
 * L'APROM est toujours accessible octet par octet aux offsets 0x00-0x05.
 * On utilise inb() car l'APROM ne dépend pas du mode WIO/DWIO.
 */
static void pcnet_read_mac(PCNetDevice *dev) {
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
static void pcnet_print_mac(PCNetDevice *dev) {
  (void)dev;
  KLOG_INFO("PCNET", "MAC Address read from device");
}

/* ============================================ */
/*           Initialization                     */
/* ============================================ */

/**
 * Vérifie et affiche l'état du CSR0.
 */
static void pcnet_print_status(PCNetDevice *dev) {
  uint32_t csr0 = pcnet_read_csr(dev, CSR0);
  KLOG_INFO_HEX("PCNET", "CSR0 Status: ", csr0);
}

/**
 * Configure le style logiciel en mode 32-bit PCnet-PCI.
 * Active SWSTYLE=2 (descripteurs 32-bit) et SSIZE32 (adresses 32-bit).
 */
static void pcnet_set_software_style(PCNetDevice *dev) {
  /* Lire BCR20 (Software Style) */
  uint32_t bcr20 = pcnet_read_bcr(dev, BCR20);

  /*
   * BCR20 bits:
   * - Bits 0-7: SWSTYLE (Software Style)
   *   - 0 = LANCE style (16-bit)
   *   - 2 = PCnet-PCI II style (32-bit descriptors)
   * - Bit 8: SSIZE32 (Software Size 32-bit)
   *   - Permet les adresses 32-bit dans l'init block et descripteurs
   *
   * On veut: SWSTYLE=2, SSIZE32=1 -> 0x0102
   */
  bcr20 = (bcr20 & ~0x01FF) | SWSTYLE_PCNET_PCI | (1 << 8);

  pcnet_write_bcr(dev, BCR20, bcr20);

  /* Vérifier que ça a pris */
  bcr20 = pcnet_read_bcr(dev, BCR20);

  if ((bcr20 & 0xFF) == SWSTYLE_PCNET_PCI) {
    KLOG_INFO("PCNET", "Software Style set to PCNET-PCI (32-bit)");
  } else {
    KLOG_ERROR("PCNET", "Failed to set SWSTYLE!");
  }
}

/**
 * Wrapper de la fonction send pour NetInterface.
 * Cette fonction est appelée via le pointeur netif->send().
 */
static int pcnet_netif_send(NetInterface *netif, uint8_t *data, int len) {
  if (netif == NULL || netif->driver_data == NULL)
    return -1;

  PCNetDevice *dev = (PCNetDevice *)netif->driver_data;

  bool result = pcnet_send(dev, data, (uint16_t)len);

  if (result) {
    netif->packets_tx++;
    netif->bytes_tx += len;
    return len;
  } else {
    netif->errors++;
    return -1;
  }
}

PCNetDevice *pcnet_init(PCIDevice *pci_dev) {
  KLOG_INFO("PCNET", "=== PCnet Driver Initialization ===");

  if (pci_dev == NULL) {
    KLOG_ERROR("PCNET", "No PCI device provided!");
    return NULL;
  }

  /* Allouer la structure du driver */
  PCNetDevice *dev = (PCNetDevice *)kmalloc(sizeof(PCNetDevice));
  if (dev == NULL) {
    KLOG_ERROR("PCNET", "Failed to allocate driver structure!");
    return NULL;
  }

  /* Initialiser la structure */
  dev->pci_dev = pci_dev;
  dev->io_base = pci_dev->bar0 & 0xFFFFFFFC; /* Masquer les bits de type */
  dev->mmio_base = NULL;
  dev->mmio_phys = 0;
  dev->mmio_size = 0;
  dev->access_mode = PCNET_ACCESS_PIO; /* Par défaut PIO */
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

  /* ============================================ */
  /* Détection et configuration du mode d'accès  */
  /* ============================================ */
  
  /* Parser les BARs PCI pour détecter MMIO */
  pci_device_bars_t bars;
  if (pci_parse_bars(pci_dev, &bars) == 0) {
    KLOG_INFO("PCNET", "Analyzing PCI BARs...");
    
    /* Chercher un BAR MMIO (généralement BAR1 pour PCnet) */
    pci_bar_info_t *mmio_bar = pci_find_mmio_bar(&bars);
    pci_bar_info_t *pio_bar = pci_find_pio_bar(&bars);
    
    /* Afficher les BARs détectés */
    if (pio_bar != NULL) {
      KLOG_INFO_HEX("PCNET", "PIO BAR: ", pio_bar->base_addr);
    }
    
    if (mmio_bar != NULL) {
      KLOG_INFO_HEX("PCNET", "MMIO BAR: ", mmio_bar->base_addr);
    }
    
    /* Sélectionner le mode d'accès */
    bool use_mmio = false;
    
    if (g_forced_access_mode == 0) {
      /* Mode PIO forcé */
      use_mmio = false;
      KLOG_INFO("PCNET", "Access mode: PIO (forced)");
    } else if (g_forced_access_mode == 1 && mmio_bar != NULL) {
      /* Mode MMIO forcé */
      use_mmio = true;
      KLOG_INFO("PCNET", "Access mode: MMIO (forced)");
    } else if (mmio_bar != NULL && mmio_bar->size >= 32) {
      /* MMIO disponible et suffisamment grand - l'utiliser */
      use_mmio = true;
      KLOG_INFO("PCNET", "Access mode: MMIO (auto-detected)");
    } else {
      /* Fallback sur PIO */
      use_mmio = false;
      KLOG_INFO("PCNET", "Access mode: PIO (fallback)");
    }
    
    if (use_mmio && mmio_bar != NULL) {
      /* Mapper la région MMIO */
      dev->mmio_phys = mmio_bar->base_addr;
      dev->mmio_size = mmio_bar->size;
      dev->mmio_base = pci_map_bar(mmio_bar);
      
      if (dev->mmio_base != NULL) {
        dev->access_mode = PCNET_ACCESS_MMIO;
        KLOG_INFO_HEX("PCNET", "MMIO mapped at virtual: ", (uint32_t)(uintptr_t)dev->mmio_base);
      } else {
        KLOG_WARN("PCNET", "Failed to map MMIO, falling back to PIO");
        dev->access_mode = PCNET_ACCESS_PIO;
      }
    }
  }

  KLOG_INFO_HEX("PCNET", "I/O Base (PIO): ", dev->io_base);
  KLOG_INFO_DEC("PCNET", "PCI Interrupt Line: ", pci_dev->interrupt_line);

  if (pci_dev->interrupt_line != 11) {
    KLOG_WARN_DEC("PCNET", "Card uses IRQ (patching IDT): ", pci_dev->interrupt_line);

    /* Utiliser le wrapper ASM existant pour IRQ 11 car il fait ce qu'on veut
     * (EOI) */
    extern void irq11_handler(void);
    idt_set_gate(32 + pci_dev->interrupt_line, (uint32_t)irq11_handler, 0x08,
                 0x8E);
  }

  /* Étape 1: Activer le Bus Mastering PCI */
  pci_enable_bus_mastering(pci_dev);

  /* Étape 2: Reset de la carte */
  KLOG_INFO("PCNET", "Resetting card...");
  pcnet_reset(dev);

  /* Petite pause post-reset */
  for (volatile int i = 0; i < 100000; i++)
    ;

  /* Étape 2b: Configurer SWSTYLE = 2 (32-bit PCnet-PCI) AVANT toute allocation
   */
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
  dev->init_block = (PCNetInitBlock *)kmalloc(sizeof(PCNetInitBlock) + 16);
  if (dev->init_block == NULL) {
    KLOG_ERROR("PCNET", "Failed to allocate Init Block!");
    kfree(dev);
    return NULL;
  }

  /* Aligner sur 16 octets si nécessaire */
  uint32_t init_block_addr = (uint32_t)(uintptr_t)dev->init_block;
  if (init_block_addr & 0xF) {
    init_block_addr = (init_block_addr + 15) & ~0xF;
    dev->init_block = (PCNetInitBlock *)(uintptr_t)init_block_addr;
  }

  KLOG_INFO_HEX("PCNET", "Init Block allocated at: ", (uint32_t)(uintptr_t)dev->init_block);

  /* Vérifier l'alignement */
  if ((uint32_t)(uintptr_t)dev->init_block & 0x3) {
    KLOG_WARN("PCNET", "Init Block not 4-byte aligned!");
  } else {
    KLOG_INFO("PCNET", "Init Block alignment: OK");
  }

  /* Étape 6: Allouer les Descriptor Rings (alignés sur 16 octets) */
  dev->rx_ring =
      (PCNetRxDesc *)kmalloc(sizeof(PCNetRxDesc) * PCNET_RX_BUFFERS + 16);
  dev->tx_ring =
      (PCNetTxDesc *)kmalloc(sizeof(PCNetTxDesc) * PCNET_TX_BUFFERS + 16);

  if (dev->rx_ring == NULL || dev->tx_ring == NULL) {
    KLOG_ERROR("PCNET", "Failed to allocate descriptor rings!");
    return NULL;
  }

  /* Aligner les rings sur 16 octets */
  uint32_t rx_ring_addr = (uint32_t)(uintptr_t)dev->rx_ring;
  if (rx_ring_addr & 0xF) {
    rx_ring_addr = (rx_ring_addr + 15) & ~0xF;
    dev->rx_ring = (PCNetRxDesc *)(uintptr_t)rx_ring_addr;
  }

  uint32_t tx_ring_addr = (uint32_t)(uintptr_t)dev->tx_ring;
  if (tx_ring_addr & 0xF) {
    tx_ring_addr = (tx_ring_addr + 15) & ~0xF;
    dev->tx_ring = (PCNetTxDesc *)(uintptr_t)tx_ring_addr;
  }

  KLOG_INFO_HEX("PCNET", "RX Ring at: ", (uint32_t)(uintptr_t)dev->rx_ring);
  KLOG_INFO_HEX("PCNET", "TX Ring at: ", (uint32_t)(uintptr_t)dev->tx_ring);

  /* Étape 7: Allouer les buffers de données */
  dev->rx_buffers = (uint8_t *)kmalloc(PCNET_BUFFER_SIZE * PCNET_RX_BUFFERS);
  dev->tx_buffers = (uint8_t *)kmalloc(PCNET_BUFFER_SIZE * PCNET_TX_BUFFERS);

  if (dev->rx_buffers == NULL || dev->tx_buffers == NULL) {
    KLOG_ERROR("PCNET", "Failed to allocate data buffers!");
    return NULL;
  }

  KLOG_INFO_HEX("PCNET", "RX Buffers at: ", (uint32_t)(uintptr_t)dev->rx_buffers);
  KLOG_INFO_HEX("PCNET", "TX Buffers at: ", (uint32_t)(uintptr_t)dev->tx_buffers);

  /* Étape 8: Initialiser les descripteurs RX */
  for (int i = 0; i < PCNET_RX_BUFFERS; i++) {
    dev->rx_ring[i].rbadr =
        (uint32_t)(uintptr_t)(dev->rx_buffers + i * PCNET_BUFFER_SIZE);
    /* BCNT: bits 12-15 doivent être 1 (0xF000), bits 0-11 = complément à 2 de
     * la taille */
    dev->rx_ring[i].bcnt =
        0xF000 | ((uint16_t)(-(int16_t)PCNET_BUFFER_SIZE) & 0x0FFF);
    dev->rx_ring[i].status = 0x8000; /* OWN = 1 (Card owned, prêt à recevoir) */
    dev->rx_ring[i].mcnt = 0;
    dev->rx_ring[i].user = 0;
  }

  /* Étape 9: Initialiser les descripteurs TX */
  for (int i = 0; i < PCNET_TX_BUFFERS; i++) {
    dev->tx_ring[i].tbadr =
        (uint32_t)(uintptr_t)(dev->tx_buffers + i * PCNET_BUFFER_SIZE);
    dev->tx_ring[i].bcnt = 0xF000; /* Bits 12-15 = 1, taille = 0 */
    dev->tx_ring[i].status = 0;    /* OWN = 0 (CPU owned) */
    dev->tx_ring[i].misc = 0;
    dev->tx_ring[i].user = 0;
  }

  KLOG_INFO_DEC("PCNET", "RX Descriptors: ", PCNET_RX_BUFFERS);
  KLOG_INFO_DEC("PCNET", "TX Descriptors: ", PCNET_TX_BUFFERS);

  /* Étape 10: Configurer l'Initialization Block */
  dev->init_block->mode = 0; /* Normal operation */
  dev->init_block->rlen =
      (PCNET_LOG2_RX_BUFFERS << 4); /* log2(16) = 4, shifted */
  dev->init_block->tlen =
      (PCNET_LOG2_TX_BUFFERS << 4); /* log2(16) = 4, shifted */

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

  KLOG_INFO("PCNET", "Init Block configured");

  /* Stocker l'instance globale */
  g_pcnet_dev = dev;
  dev->initialized = false; /* Sera mis à true par pcnet_start */

  /* ============================================ */
  /* Créer et enregistrer la NetInterface         */
  /* ============================================ */

  /* Allouer la NetInterface */
  g_pcnet_netif = (NetInterface *)kmalloc(sizeof(NetInterface));
  if (g_pcnet_netif != NULL) {
    /* Initialiser la structure à zéro */
    uint8_t *ptr = (uint8_t *)g_pcnet_netif;
    for (unsigned int i = 0; i < sizeof(NetInterface); i++) {
      ptr[i] = 0;
    }

    /* Nom de l'interface */
    g_pcnet_netif->name[0] = 'e';
    g_pcnet_netif->name[1] = 't';
    g_pcnet_netif->name[2] = 'h';
    g_pcnet_netif->name[3] = '0';
    g_pcnet_netif->name[4] = '\0';

    /* Copier l'adresse MAC depuis l'EEPROM */
    for (int i = 0; i < 6; i++) {
      g_pcnet_netif->mac_addr[i] = dev->mac_addr[i];
    }

    /* Configuration IP initialement vide (sera configurée par DHCP ou
     * manuellement) */
    g_pcnet_netif->ip_addr = 0;
    g_pcnet_netif->netmask = 0;
    g_pcnet_netif->gateway = 0;
    g_pcnet_netif->dns_server = 0;

    /* Interface down par défaut (sera mise UP par pcnet_start) */
    g_pcnet_netif->flags = NETIF_FLAG_DOWN;

    /* Assigner la fonction d'envoi */
    g_pcnet_netif->send = pcnet_netif_send;

    /* Données du driver */
    g_pcnet_netif->driver_data = dev;

    /* Enregistrer l'interface */
    netdev_register(g_pcnet_netif);
  }

  KLOG_INFO("PCNET", "Driver initialized successfully!");

  return dev;
}

bool pcnet_start(PCNetDevice *dev) {
  if (dev == NULL)
    return false;

  KLOG_INFO("PCNET", "Starting card...");

  /* Étape 1: Écrire l'adresse de l'Init Block dans CSR1 et CSR2 */
  uint32_t init_addr = (uint32_t)(uintptr_t)dev->init_block;

  pcnet_write_csr(dev, CSR1, init_addr & 0xFFFF);         /* 16 bits bas */
  pcnet_write_csr(dev, CSR2, (init_addr >> 16) & 0xFFFF); /* 16 bits hauts */

  KLOG_INFO_HEX("PCNET", "Init Block address written: ", init_addr);

  /* Étape 2: Lancer l'initialisation SANS interruptions (juste INIT) */
  pcnet_write_csr(dev, CSR0, CSR0_INIT);

  KLOG_INFO("PCNET", "Waiting for IDON...");

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
    KLOG_ERROR_HEX("PCNET", "Timeout waiting for IDON! CSR0: ", csr0);
    return false;
  }

  KLOG_INFO_HEX("PCNET", "IDON received! CSR0: ", csr0);

  /* Acquitter IDON en écrivant 1 */
  pcnet_write_csr(dev, CSR0, CSR0_IDON);

  /* Étape 4: Démarrer la carte (STRT + IENA pour activer les interruptions) */
  pcnet_write_csr(dev, CSR0, CSR0_STRT | CSR0_IENA);

  /* Vérifier que la carte est démarrée */
  csr0 = pcnet_read_csr(dev, CSR0);

  KLOG_INFO_HEX("PCNET", "After START, CSR0: ", csr0);

  dev->initialized = true;

  /* Mettre à jour les flags de la NetInterface */
  if (g_pcnet_netif != NULL) {
    g_pcnet_netif->flags &= ~NETIF_FLAG_DOWN;
    g_pcnet_netif->flags |= NETIF_FLAG_UP | NETIF_FLAG_RUNNING;
  }

  KLOG_INFO("PCNET", "PCnet Started! Ready to send/receive packets");

  return true;
}

/**
 * Envoie un paquet Ethernet.
 */
bool pcnet_send(PCNetDevice *dev, const uint8_t *data, uint16_t len) {
  if (dev == NULL || data == NULL || len == 0)
    return false;
  if (len > PCNET_BUFFER_SIZE)
    return false;

  /* Obtenir le descripteur TX actuel */
  int idx = dev->tx_index;
  PCNetTxDesc *desc = &dev->tx_ring[idx];

  /* Vérifier que le descripteur est libre (OWN = 0) */
  if (desc->status & 0x8000) {
    /* Descripteur encore possédé par la carte */
    KLOG_WARN("PCNET", "TX buffer busy!");
    return false;
  }

  /* Copier les données dans le buffer */
  uint8_t *buf = dev->tx_buffers + idx * PCNET_BUFFER_SIZE;
  for (uint16_t i = 0; i < len; i++) {
    buf[i] = data[i];
  }

  /* Configurer le descripteur */
  desc->tbadr = (uint32_t)(uintptr_t)buf;

  /*
   * BCNT: 12 bits, complément à 2, bits 15-12 doivent être 1 (0xF000)
   * Pour SWSTYLE 2, bcnt est dans les bits 0-15 du deuxième DWORD
   */
  uint16_t bcnt_val = 0xF000 | ((uint16_t)(-(int16_t)len) & 0x0FFF);

  /*
   * Status: OWN=1 (card owns), STP=1 (start of packet), ENP=1 (end of packet)
   * OWN = bit 15, STP = bit 9, ENP = bit 8
   * 0x8300 = 0x8000 | 0x0200 | 0x0100
   */
  uint16_t status_val = 0x8300;

  /* Écrire les deux champs - l'ordre compte sur little-endian */
  desc->bcnt = bcnt_val;
  desc->status = status_val;
  desc->misc = 0;

  /* Passer au descripteur suivant */
  dev->tx_index = (dev->tx_index + 1) % PCNET_TX_BUFFERS;

  /* Déclencher l'envoi immédiat avec TDMD + garder IENA actif */
  pcnet_write_csr(dev, CSR0, CSR0_TDMD | CSR0_IENA);

  return true;
}

void pcnet_stop(PCNetDevice *dev) {
  if (dev == NULL)
    return;

  /* Écrire STOP dans CSR0 */
  pcnet_write_csr(dev, CSR0, CSR0_STOP);

  KLOG_INFO("PCNET", "Card stopped");
}

PCNetDevice *pcnet_get_device(void) { return g_pcnet_dev; }

void pcnet_get_mac(uint8_t *buf) {
  if (buf == NULL)
    return;

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
