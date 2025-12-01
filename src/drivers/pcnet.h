/* src/drivers/pcnet.h - AMD PCnet-PCI II (Am79C970A) Driver */
#ifndef PCNET_H
#define PCNET_H

#include <stdint.h>
#include <stdbool.h>
#include "../pci.h"

/* ============================================ */
/*           Registres I/O (Mode DWIO 32-bit)   */
/* ============================================ */

/* Offsets depuis BAR0 en mode WIO (16-bit) - QEMU utilise ce mode */
#define PCNET_APROM0    0x00    /* EEPROM/MAC Address bytes 0-3 */
#define PCNET_APROM4    0x04    /* EEPROM/MAC Address bytes 4-5 */
#define PCNET_RDP       0x10    /* Register Data Port (lecture/écriture CSR) */
#define PCNET_RAP       0x12    /* Register Address Port (sélection CSR/BCR) */
#define PCNET_RESET     0x14    /* Reset Register (lecture = reset) */
#define PCNET_BDP       0x16    /* Bus Configuration Register Data Port */

/* ============================================ */
/*           Control and Status Registers (CSR) */
/* ============================================ */

#define CSR0    0       /* Controller Status Register */
#define CSR1    1       /* Initialization Block Address (low 16 bits) */
#define CSR2    2       /* Initialization Block Address (high 16 bits) */
#define CSR3    3       /* Interrupt Masks and Deferral Control */
#define CSR4    4       /* Test and Features Control */
#define CSR5    5       /* Extended Control and Interrupt */
#define CSR15   15      /* Mode Register */
#define CSR58   58      /* Software Style (pour activer DWIO) */
#define CSR88   88      /* Chip ID (low) */
#define CSR89   89      /* Chip ID (high) */

/* CSR0 Bits */
#define CSR0_INIT   (1 << 0)    /* Initialize */
#define CSR0_STRT   (1 << 1)    /* Start */
#define CSR0_STOP   (1 << 2)    /* Stop */
#define CSR0_TDMD   (1 << 3)    /* Transmit Demand */
#define CSR0_TXON   (1 << 4)    /* Transmit ON */
#define CSR0_RXON   (1 << 5)    /* Receive ON */
#define CSR0_IENA   (1 << 6)    /* Interrupt Enable */
#define CSR0_INTR   (1 << 7)    /* Interrupt Flag */
#define CSR0_IDON   (1 << 8)    /* Initialization Done */
#define CSR0_TINT   (1 << 9)    /* Transmit Interrupt */
#define CSR0_RINT   (1 << 10)   /* Receive Interrupt */
#define CSR0_MERR   (1 << 11)   /* Memory Error */
#define CSR0_MISS   (1 << 12)   /* Missed Frame */
#define CSR0_CERR   (1 << 13)   /* Collision Error */
#define CSR0_BABL   (1 << 14)   /* Babble (transmit timeout) */
#define CSR0_ERR    (1 << 15)   /* Error (OR of BABL, CERR, MISS, MERR) */

/* CSR3 Bits */
#define CSR3_BSWP   (1 << 2)    /* Byte Swap */
#define CSR3_EMBA   (1 << 3)    /* Enable Modified Back-off Algorithm */
#define CSR3_DXMT2PD (1 << 4)   /* Disable Transmit Two Part Deferral */
#define CSR3_LAPPEN (1 << 5)    /* Look Ahead Packet Processing Enable */
#define CSR3_DXSUFLO (1 << 6)   /* Disable Transmit Stop on Underflow */
#define CSR3_IDONM  (1 << 8)    /* Initialization Done Mask */
#define CSR3_TINTM  (1 << 9)    /* Transmit Interrupt Mask */
#define CSR3_RINTM  (1 << 10)   /* Receive Interrupt Mask */
#define CSR3_MERRM  (1 << 11)   /* Memory Error Mask */
#define CSR3_MISSM  (1 << 12)   /* Missed Frame Mask */

/* CSR4 Bits */
#define CSR4_ASTRP_RCV (1 << 10) /* Auto Strip Receive */
#define CSR4_APAD_XMT  (1 << 11) /* Auto Pad Transmit */

/* CSR15 Bits (Mode) */
#define CSR15_DRX   (1 << 0)    /* Disable Receiver */
#define CSR15_DTX   (1 << 1)    /* Disable Transmitter */
#define CSR15_LOOP  (1 << 2)    /* Loopback Enable */
#define CSR15_PROMISC (1 << 15) /* Promiscuous Mode */

/* ============================================ */
/*       Bus Configuration Registers (BCR)      */
/* ============================================ */

#define BCR2    2       /* Miscellaneous Configuration */
#define BCR18   18      /* Burst and Bus Control Register */
#define BCR20   20      /* Software Style */

/* BCR18 Bits */
#define BCR18_BREADE (1 << 6)   /* Burst Read Enable */
#define BCR18_BWRITE (1 << 7)   /* Burst Write Enable */

/* BCR20 - Software Style */
#define SWSTYLE_LANCE       0   /* 16-bit Lance/PCnet-ISA */
#define SWSTYLE_ILACC       1   /* 32-bit ILACC */
#define SWSTYLE_PCNET_PCI   2   /* 32-bit PCnet-PCI (ce qu'on veut) */

/* ============================================ */
/*           Descriptor Ring Sizes              */
/* ============================================ */

#define PCNET_LOG2_RX_BUFFERS   4   /* 16 receive buffers */
#define PCNET_LOG2_TX_BUFFERS   4   /* 16 transmit buffers */
#define PCNET_RX_BUFFERS        (1 << PCNET_LOG2_RX_BUFFERS)
#define PCNET_TX_BUFFERS        (1 << PCNET_LOG2_TX_BUFFERS)
#define PCNET_BUFFER_SIZE       1544    /* MTU + headers */

/* ============================================ */
/*           Initialization Block               */
/* ============================================ */

/**
 * Initialization Block pour PCnet-PCI II (32-bit Software Style 2)
 * Doit être aligné sur 4 octets minimum (idéalement 16)
 */
typedef struct __attribute__((packed)) {
    uint16_t mode;          /* Mode Register (CSR15 copy) */
    uint8_t  rlen;          /* Receive Ring Length (encoded: log2(n) << 4) */
    uint8_t  tlen;          /* Transmit Ring Length (encoded: log2(n) << 4) */
    uint8_t  padr[6];       /* Physical Address (MAC) */
    uint16_t reserved;      /* Reserved (must be 0) */
    uint8_t  ladr[8];       /* Logical Address Filter (multicast) */
    uint32_t rdra;          /* Receive Descriptor Ring Address */
    uint32_t tdra;          /* Transmit Descriptor Ring Address */
} PCNetInitBlock;

/**
 * Receive Descriptor (32-bit Software Style 2)
 */
typedef struct __attribute__((packed)) {
    uint32_t rbadr;         /* Receive Buffer Address */
    int16_t  bcnt;          /* Buffer Byte Count (two's complement, negative) */
    uint16_t status;        /* Status bits */
    uint32_t mcnt;          /* Message Byte Count (received length) */
    uint32_t user;          /* User data (unused) */
} PCNetRxDesc;

/**
 * Transmit Descriptor (32-bit Software Style 2)
 */
typedef struct __attribute__((packed)) {
    uint32_t tbadr;         /* Transmit Buffer Address */
    int16_t  bcnt;          /* Buffer Byte Count (two's complement, negative) */
    uint16_t status;        /* Status bits */
    uint32_t misc;          /* Miscellaneous (errors) */
    uint32_t user;          /* User data (unused) */
} PCNetTxDesc;

/* Descriptor Status Bits */
#define DESC_OWN    (1 << 15)   /* Owned by controller (1) or host (0) */
#define DESC_ERR    (1 << 14)   /* Error occurred */
#define DESC_STP    (1 << 9)    /* Start of Packet */
#define DESC_ENP    (1 << 8)    /* End of Packet */

/* ============================================ */
/*           Driver State Structure             */
/* ============================================ */

typedef struct {
    PCIDevice*      pci_dev;        /* PCI device info */
    uint32_t        io_base;        /* I/O Base Address (from BAR0) */
    uint8_t         mac_addr[6];    /* MAC Address */
    
    /* DMA Buffers (physiquement contigus) */
    PCNetInitBlock* init_block;     /* Initialization Block */
    PCNetRxDesc*    rx_ring;        /* Receive Descriptor Ring */
    PCNetTxDesc*    tx_ring;        /* Transmit Descriptor Ring */
    uint8_t*        rx_buffers;     /* Receive Buffers */
    uint8_t*        tx_buffers;     /* Transmit Buffers */
    
    /* Ring indices */
    int             rx_index;       /* Current receive index */
    int             tx_index;       /* Current transmit index */
    
    /* Statistics */
    uint32_t        packets_rx;
    uint32_t        packets_tx;
    uint32_t        errors;
    
    bool            initialized;
} PCNetDevice;

/* ============================================ */
/*           Public Functions                   */
/* ============================================ */

/**
 * Active le Bus Mastering PCI pour un périphérique.
 * Nécessaire pour que le DMA fonctionne.
 */
void pci_enable_bus_mastering(PCIDevice* dev);

/**
 * Initialise le driver PCnet pour le périphérique donné.
 * Lit l'adresse MAC, prépare les structures DMA.
 * 
 * @param dev Le périphérique PCI AMD PCnet
 * @return Pointeur vers la structure PCNetDevice ou NULL si échec
 */
PCNetDevice* pcnet_init(PCIDevice* dev);

/**
 * Lit un CSR (Control and Status Register).
 */
uint32_t pcnet_read_csr(PCNetDevice* dev, uint32_t csr_no);

/**
 * Écrit dans un CSR.
 */
void pcnet_write_csr(PCNetDevice* dev, uint32_t csr_no, uint32_t value);

/**
 * Lit un BCR (Bus Configuration Register).
 */
uint32_t pcnet_read_bcr(PCNetDevice* dev, uint32_t bcr_no);

/**
 * Écrit dans un BCR.
 */
void pcnet_write_bcr(PCNetDevice* dev, uint32_t bcr_no, uint32_t value);

/**
 * Démarre la carte (active TX/RX).
 */
bool pcnet_start(PCNetDevice* dev);

/**
 * Arrête la carte.
 */
void pcnet_stop(PCNetDevice* dev);

/**
 * Handler d'interruption PCnet (appelé par IRQ 11).
 */
void pcnet_irq_handler(void);

/**
 * Retourne le périphérique PCnet global (s'il est initialisé).
 */
PCNetDevice* pcnet_get_device(void);

#endif /* PCNET_H */
