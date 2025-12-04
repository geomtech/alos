/* src/drivers/net/e1000e.h - Intel e1000e Network Driver
 *
 * Driver pour les cartes réseau Intel 82574L et compatibles e1000e.
 * Utilise MMIO pour l'accès aux registres.
 */
#ifndef E1000E_H
#define E1000E_H

#include "../pci.h"
#include <stdbool.h>
#include <stdint.h>

/* ============================================ */
/*           PCI Device IDs                     */
/* ============================================ */

#define E1000E_VENDOR_ID        0x8086  /* Intel */

/* Device IDs supportés */
#define E1000E_DEV_82540EM      0x100E  /* QEMU default */
#define E1000E_DEV_82545EM      0x100F
#define E1000E_DEV_82574L       0x10D3  /* e1000e */
#define E1000E_DEV_82579LM      0x1502
#define E1000E_DEV_82579V       0x1503
#define E1000E_DEV_I217LM       0x153A
#define E1000E_DEV_I217V        0x153B
#define E1000E_DEV_I218LM       0x155A
#define E1000E_DEV_I218V        0x1559
#define E1000E_DEV_I219LM       0x156F
#define E1000E_DEV_I219V        0x1570

/* ============================================ */
/*           Register Offsets (MMIO)            */
/* ============================================ */

/* Device Control */
#define E1000_CTRL              0x0000  /* Device Control Register */
#define E1000_STATUS            0x0008  /* Device Status Register */
#define E1000_EECD              0x0010  /* EEPROM/Flash Control/Data */
#define E1000_EERD              0x0014  /* EEPROM Read */
#define E1000_CTRL_EXT          0x0018  /* Extended Device Control */
#define E1000_MDIC              0x0020  /* MDI Control */
#define E1000_FCAL              0x0028  /* Flow Control Address Low */
#define E1000_FCAH              0x002C  /* Flow Control Address High */
#define E1000_FCT               0x0030  /* Flow Control Type */
#define E1000_VET               0x0038  /* VLAN Ether Type */
#define E1000_ICR               0x00C0  /* Interrupt Cause Read */
#define E1000_ITR               0x00C4  /* Interrupt Throttling Rate */
#define E1000_ICS               0x00C8  /* Interrupt Cause Set */
#define E1000_IMS               0x00D0  /* Interrupt Mask Set/Read */
#define E1000_IMC               0x00D8  /* Interrupt Mask Clear */
#define E1000_IAM               0x00E0  /* Interrupt Acknowledge Auto Mask */

/* Receive Control */
#define E1000_RCTL              0x0100  /* Receive Control */
#define E1000_FCTTV             0x0170  /* Flow Control Transmit Timer Value */
#define E1000_TXCW              0x0178  /* Transmit Configuration Word */
#define E1000_RXCW              0x0180  /* Receive Configuration Word */

/* Transmit Control */
#define E1000_TCTL              0x0400  /* Transmit Control */
#define E1000_TCTL_EXT          0x0404  /* Extended Transmit Control */
#define E1000_TIPG              0x0410  /* Transmit Inter-Packet Gap */

/* Receive Descriptor */
#define E1000_RDBAL             0x2800  /* RX Descriptor Base Address Low */
#define E1000_RDBAH             0x2804  /* RX Descriptor Base Address High */
#define E1000_RDLEN             0x2808  /* RX Descriptor Length */
#define E1000_RDH               0x2810  /* RX Descriptor Head */
#define E1000_RDT               0x2818  /* RX Descriptor Tail */
#define E1000_RDTR              0x2820  /* RX Delay Timer */
#define E1000_RXDCTL            0x2828  /* RX Descriptor Control */
#define E1000_RADV              0x282C  /* RX Interrupt Absolute Delay Timer */
#define E1000_RSRPD             0x2C00  /* RX Small Packet Detect */

/* Transmit Descriptor */
#define E1000_TDBAL             0x3800  /* TX Descriptor Base Address Low */
#define E1000_TDBAH             0x3804  /* TX Descriptor Base Address High */
#define E1000_TDLEN             0x3808  /* TX Descriptor Length */
#define E1000_TDH               0x3810  /* TX Descriptor Head */
#define E1000_TDT               0x3818  /* TX Descriptor Tail */
#define E1000_TIDV              0x3820  /* TX Interrupt Delay Value */
#define E1000_TXDCTL            0x3828  /* TX Descriptor Control */
#define E1000_TADV              0x382C  /* TX Interrupt Absolute Delay Val */

/* Receive Address */
#define E1000_RAL0              0x5400  /* Receive Address Low (0) */
#define E1000_RAH0              0x5404  /* Receive Address High (0) */

/* Multicast Table Array */
#define E1000_MTA               0x5200  /* Multicast Table Array (128 entries) */

/* Statistics Registers */
#define E1000_CRCERRS           0x4000  /* CRC Error Count */
#define E1000_ALGNERRC          0x4004  /* Alignment Error Count */
#define E1000_RXERRC            0x400C  /* RX Error Count */
#define E1000_MPC               0x4010  /* Missed Packets Count */
#define E1000_COLC              0x4028  /* Collision Count */
#define E1000_TPR               0x40D0  /* Total Packets Received */
#define E1000_TPT               0x40D4  /* Total Packets Transmitted */

/* ============================================ */
/*           Control Register Bits              */
/* ============================================ */

/* CTRL Register */
#define E1000_CTRL_FD           (1 << 0)    /* Full Duplex */
#define E1000_CTRL_LRST         (1 << 3)    /* Link Reset */
#define E1000_CTRL_ASDE         (1 << 5)    /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU          (1 << 6)    /* Set Link Up */
#define E1000_CTRL_ILOS         (1 << 7)    /* Invert Loss-of-Signal */
#define E1000_CTRL_SPEED_MASK   (3 << 8)    /* Speed selection */
#define E1000_CTRL_SPEED_10     (0 << 8)
#define E1000_CTRL_SPEED_100    (1 << 8)
#define E1000_CTRL_SPEED_1000   (2 << 8)
#define E1000_CTRL_FRCSPD       (1 << 11)   /* Force Speed */
#define E1000_CTRL_FRCDPX       (1 << 12)   /* Force Duplex */
#define E1000_CTRL_RST          (1 << 26)   /* Device Reset */
#define E1000_CTRL_VME          (1 << 30)   /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST      (1 << 31)   /* PHY Reset */

/* STATUS Register */
#define E1000_STATUS_FD         (1 << 0)    /* Full Duplex */
#define E1000_STATUS_LU         (1 << 1)    /* Link Up */
#define E1000_STATUS_TXOFF      (1 << 4)    /* Transmission Paused */
#define E1000_STATUS_SPEED_MASK (3 << 6)    /* Link Speed */
#define E1000_STATUS_SPEED_10   (0 << 6)
#define E1000_STATUS_SPEED_100  (1 << 6)
#define E1000_STATUS_SPEED_1000 (2 << 6)

/* RCTL Register */
#define E1000_RCTL_EN           (1 << 1)    /* Receiver Enable */
#define E1000_RCTL_SBP          (1 << 2)    /* Store Bad Packets */
#define E1000_RCTL_UPE          (1 << 3)    /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE          (1 << 4)    /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE          (1 << 5)    /* Long Packet Reception Enable */
#define E1000_RCTL_LBM_MASK     (3 << 6)    /* Loopback Mode */
#define E1000_RCTL_LBM_NO       (0 << 6)    /* No Loopback */
#define E1000_RCTL_RDMTS_HALF   (0 << 8)    /* RX Desc Min Threshold Size */
#define E1000_RCTL_RDMTS_QUAR   (1 << 8)
#define E1000_RCTL_RDMTS_EIGHTH (2 << 8)
#define E1000_RCTL_MO_36        (0 << 12)   /* Multicast Offset - bits 47:36 */
#define E1000_RCTL_MO_35        (1 << 12)   /* Multicast Offset - bits 46:35 */
#define E1000_RCTL_MO_34        (2 << 12)   /* Multicast Offset - bits 45:34 */
#define E1000_RCTL_MO_32        (3 << 12)   /* Multicast Offset - bits 43:32 */
#define E1000_RCTL_BAM          (1 << 15)   /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048   (0 << 16)   /* Buffer Size 2048 */
#define E1000_RCTL_BSIZE_1024   (1 << 16)   /* Buffer Size 1024 */
#define E1000_RCTL_BSIZE_512    (2 << 16)   /* Buffer Size 512 */
#define E1000_RCTL_BSIZE_256    (3 << 16)   /* Buffer Size 256 */
#define E1000_RCTL_BSIZE_16384  ((3 << 16) | (1 << 25))  /* Buffer Size 16384 */
#define E1000_RCTL_VFE          (1 << 18)   /* VLAN Filter Enable */
#define E1000_RCTL_CFIEN        (1 << 19)   /* Canonical Form Indicator Enable */
#define E1000_RCTL_CFI          (1 << 20)   /* Canonical Form Indicator bit */
#define E1000_RCTL_DPF          (1 << 22)   /* Discard Pause Frames */
#define E1000_RCTL_PMCF         (1 << 23)   /* Pass MAC Control Frames */
#define E1000_RCTL_BSEX         (1 << 25)   /* Buffer Size Extension */
#define E1000_RCTL_SECRC        (1 << 26)   /* Strip Ethernet CRC */

/* TCTL Register */
#define E1000_TCTL_EN           (1 << 1)    /* Transmit Enable */
#define E1000_TCTL_PSP          (1 << 3)    /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT     4           /* Collision Threshold */
#define E1000_TCTL_COLD_SHIFT   12          /* Collision Distance */
#define E1000_TCTL_SWXOFF       (1 << 22)   /* Software XOFF Transmission */
#define E1000_TCTL_RTLC         (1 << 24)   /* Re-transmit on Late Collision */

/* Interrupt Bits */
#define E1000_ICR_TXDW          (1 << 0)    /* TX Descriptor Written Back */
#define E1000_ICR_TXQE          (1 << 1)    /* TX Queue Empty */
#define E1000_ICR_LSC           (1 << 2)    /* Link Status Change */
#define E1000_ICR_RXSEQ         (1 << 3)    /* RX Sequence Error */
#define E1000_ICR_RXDMT0        (1 << 4)    /* RX Desc Min Threshold Reached */
#define E1000_ICR_RXO           (1 << 6)    /* RX Overrun */
#define E1000_ICR_RXT0          (1 << 7)    /* RX Timer Interrupt */
#define E1000_ICR_MDAC          (1 << 9)    /* MDIO Access Complete */
#define E1000_ICR_PHYINT        (1 << 12)   /* PHY Interrupt */
#define E1000_ICR_TXD_LOW       (1 << 15)   /* TX Desc Low Threshold */
#define E1000_ICR_SRPD          (1 << 16)   /* Small Receive Packet Detected */

/* EEPROM Read Register */
#define E1000_EERD_START        (1 << 0)    /* Start Read */
#define E1000_EERD_DONE         (1 << 4)    /* Read Done */
#define E1000_EERD_ADDR_SHIFT   8           /* Address Shift */
#define E1000_EERD_DATA_SHIFT   16          /* Data Shift */

/* ============================================ */
/*           Descriptor Structures              */
/* ============================================ */

/* Receive Descriptor (Legacy) */
typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;   /* Address of the descriptor's data buffer */
    uint16_t length;        /* Length of data DMAed into buffer */
    uint16_t checksum;      /* Packet checksum */
    uint8_t  status;        /* Descriptor status */
    uint8_t  errors;        /* Descriptor errors */
    uint16_t special;       /* Special field (VLAN tag) */
} E1000RxDesc;

/* Receive Descriptor Status Bits */
#define E1000_RXD_STAT_DD       (1 << 0)    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      (1 << 1)    /* End of Packet */
#define E1000_RXD_STAT_IXSM     (1 << 2)    /* Ignore Checksum Indication */
#define E1000_RXD_STAT_VP       (1 << 3)    /* Packet is 802.1Q */
#define E1000_RXD_STAT_TCPCS    (1 << 5)    /* TCP Checksum Calculated */
#define E1000_RXD_STAT_IPCS     (1 << 6)    /* IP Checksum Calculated */
#define E1000_RXD_STAT_PIF      (1 << 7)    /* Passed In-exact Filter */

/* Transmit Descriptor (Legacy) */
typedef struct __attribute__((packed)) {
    uint64_t buffer_addr;   /* Address of the descriptor's data buffer */
    uint16_t length;        /* Data buffer length */
    uint8_t  cso;           /* Checksum offset */
    uint8_t  cmd;           /* Descriptor command */
    uint8_t  status;        /* Descriptor status */
    uint8_t  css;           /* Checksum start */
    uint16_t special;       /* Special field (VLAN tag) */
} E1000TxDesc;

/* Transmit Descriptor Command Bits */
#define E1000_TXD_CMD_EOP       (1 << 0)    /* End of Packet */
#define E1000_TXD_CMD_IFCS      (1 << 1)    /* Insert FCS */
#define E1000_TXD_CMD_IC        (1 << 2)    /* Insert Checksum */
#define E1000_TXD_CMD_RS        (1 << 3)    /* Report Status */
#define E1000_TXD_CMD_RPS       (1 << 4)    /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT      (1 << 5)    /* Descriptor Extension */
#define E1000_TXD_CMD_VLE       (1 << 6)    /* VLAN Packet Enable */
#define E1000_TXD_CMD_IDE       (1 << 7)    /* Interrupt Delay Enable */

/* Transmit Descriptor Status Bits */
#define E1000_TXD_STAT_DD       (1 << 0)    /* Descriptor Done */
#define E1000_TXD_STAT_EC       (1 << 1)    /* Excess Collisions */
#define E1000_TXD_STAT_LC       (1 << 2)    /* Late Collision */
#define E1000_TXD_STAT_TU       (1 << 3)    /* Transmit Underrun */

/* ============================================ */
/*           Driver Configuration               */
/* ============================================ */

#define E1000_NUM_RX_DESC       32          /* Number of RX descriptors */
#define E1000_NUM_TX_DESC       32          /* Number of TX descriptors */
#define E1000_RX_BUFFER_SIZE    2048        /* RX buffer size */
#define E1000_TX_BUFFER_SIZE    2048        /* TX buffer size */

/* ============================================ */
/*           Driver Structure                   */
/* ============================================ */

typedef struct {
    PCIDevice *pci_dev;             /* PCI device info */
    
    /* MMIO access */
    volatile void *mmio_base;       /* MMIO Base Address (mapped) */
    uint32_t mmio_phys;             /* Physical address of MMIO */
    uint32_t mmio_size;             /* Size of MMIO region */
    
    /* MAC Address */
    uint8_t mac_addr[6];
    
    /* Descriptors (16-byte aligned) */
    E1000RxDesc *rx_descs;          /* RX descriptor ring */
    E1000TxDesc *tx_descs;          /* TX descriptor ring */
    
    /* Buffers */
    uint8_t *rx_buffers[E1000_NUM_RX_DESC];
    uint8_t *tx_buffers[E1000_NUM_TX_DESC];
    
    /* Ring indices */
    uint16_t rx_cur;                /* Current RX descriptor */
    uint16_t tx_cur;                /* Current TX descriptor */
    
    /* IRQ */
    uint8_t irq;
    
    /* State */
    bool initialized;
    bool link_up;
    
    /* Statistics */
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t errors;
} E1000Device;

/* ============================================ */
/*           Public Functions                   */
/* ============================================ */

/**
 * Initialize the e1000e driver for the given PCI device.
 * 
 * @param pci_dev PCI device structure
 * @return Pointer to E1000Device or NULL on failure
 */
E1000Device *e1000e_init(PCIDevice *pci_dev);

/**
 * Start the device (enable RX/TX).
 * 
 * @param dev E1000 device
 * @return true on success
 */
bool e1000e_start(E1000Device *dev);

/**
 * Send a packet.
 * 
 * @param dev E1000 device
 * @param data Packet data
 * @param len Packet length
 * @return true on success
 */
bool e1000e_send(E1000Device *dev, const uint8_t *data, uint16_t len);

/**
 * IRQ handler for e1000e.
 */
void e1000e_irq_handler(void);

/**
 * Poll for received packets (non-interrupt mode).
 */
void e1000e_poll(void);

/**
 * Get the global e1000e device instance.
 * 
 * @return Pointer to E1000Device or NULL
 */
E1000Device *e1000e_get_device(void);

/**
 * Get MAC address.
 * 
 * @param dev E1000 device
 * @param buf Buffer to store MAC (6 bytes)
 */
void e1000e_get_mac(E1000Device *dev, uint8_t *buf);

/**
 * Check if a PCI device is a supported e1000/e1000e device.
 * 
 * @param vendor_id PCI vendor ID
 * @param device_id PCI device ID
 * @return true if supported
 */
bool e1000e_is_supported(uint16_t vendor_id, uint16_t device_id);

#endif /* E1000E_H */
