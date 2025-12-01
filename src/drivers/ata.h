/* src/drivers/ata.h - ATA PIO Mode Driver */
#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* ===========================================
 * IDE Primary Controller Ports (Base 0x1F0)
 * =========================================== */
#define ATA_PRIMARY_DATA         0x1F0   /* Data Register (R/W) */
#define ATA_PRIMARY_ERROR        0x1F1   /* Error Register (R) / Features (W) */
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2   /* Sector Count Register */
#define ATA_PRIMARY_LBA_LOW      0x1F3   /* LBA Low (bits 0-7) */
#define ATA_PRIMARY_LBA_MID      0x1F4   /* LBA Mid (bits 8-15) */
#define ATA_PRIMARY_LBA_HIGH     0x1F5   /* LBA High (bits 16-23) */
#define ATA_PRIMARY_DRIVE_HEAD   0x1F6   /* Drive/Head Register */
#define ATA_PRIMARY_COMMAND      0x1F7   /* Command (W) / Status (R) */
#define ATA_PRIMARY_STATUS       0x1F7   /* Alias for clarity */

/* Control/Alt Status port (for polling without clearing IRQ) */
#define ATA_PRIMARY_CONTROL      0x3F6

/* ===========================================
 * ATA Commands
 * =========================================== */
#define ATA_CMD_READ_PIO         0x20    /* Read Sectors (PIO) */
#define ATA_CMD_READ_PIO_EXT     0x24    /* Read Sectors Ext (48-bit LBA) */
#define ATA_CMD_WRITE_PIO        0x30    /* Write Sectors (PIO) */
#define ATA_CMD_WRITE_PIO_EXT    0x34    /* Write Sectors Ext (48-bit LBA) */
#define ATA_CMD_IDENTIFY         0xEC    /* Identify Device */
#define ATA_CMD_CACHE_FLUSH      0xE7    /* Cache Flush */

/* ===========================================
 * Status Register Bits
 * =========================================== */
#define ATA_SR_BSY               0x80    /* Busy */
#define ATA_SR_DRDY              0x40    /* Device Ready */
#define ATA_SR_DF                0x20    /* Device Fault */
#define ATA_SR_DSC               0x10    /* Device Seek Complete */
#define ATA_SR_DRQ               0x08    /* Data Request */
#define ATA_SR_CORR              0x04    /* Corrected Data */
#define ATA_SR_IDX               0x02    /* Index */
#define ATA_SR_ERR               0x01    /* Error */

/* ===========================================
 * Error Register Bits
 * =========================================== */
#define ATA_ER_BBK               0x80    /* Bad Block */
#define ATA_ER_UNC               0x40    /* Uncorrectable Data */
#define ATA_ER_MC                0x20    /* Media Changed */
#define ATA_ER_IDNF              0x10    /* ID Not Found */
#define ATA_ER_MCR               0x08    /* Media Change Request */
#define ATA_ER_ABRT              0x04    /* Command Aborted */
#define ATA_ER_TK0NF             0x02    /* Track 0 Not Found */
#define ATA_ER_AMNF              0x01    /* Address Mark Not Found */

/* ===========================================
 * Drive Selection
 * =========================================== */
#define ATA_DRIVE_MASTER         0xE0    /* Master drive (LBA mode) */
#define ATA_DRIVE_SLAVE          0xF0    /* Slave drive (LBA mode) */

/* ===========================================
 * Constants
 * =========================================== */
#define ATA_SECTOR_SIZE          512     /* Bytes per sector */

/* ===========================================
 * Function Prototypes
 * =========================================== */

/**
 * Initialise le contrôleur ATA.
 * @return 0 si succès, -1 si aucun disque détecté
 */
int ata_init(void);

/**
 * Attend que le contrôleur ne soit plus occupé (BSY = 0).
 */
void ata_wait_busy(void);

/**
 * Attend que le bit DRQ soit set (données prêtes).
 */
void ata_wait_drq(void);

/**
 * Lit des secteurs depuis le disque en mode PIO.
 * @param lba     Adresse LBA du premier secteur à lire
 * @param count   Nombre de secteurs à lire (1-256, 0 = 256)
 * @param buffer  Buffer de destination (doit être >= count * 512 octets)
 * @return 0 si succès, -1 si erreur
 */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);

/**
 * Écrit des secteurs sur le disque en mode PIO.
 * @param lba     Adresse LBA du premier secteur à écrire
 * @param count   Nombre de secteurs à écrire (1-256, 0 = 256)
 * @param buffer  Buffer source (doit être >= count * 512 octets)
 * @return 0 si succès, -1 si erreur
 */
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);

/**
 * Vérifie si un disque est présent sur le bus Primary Master.
 * @return 1 si présent, 0 sinon
 */
int ata_is_present(void);

/**
 * Handler d'interruption ATA (IRQ 14).
 * Appelé depuis l'assembleur.
 */
void ata_irq_handler(void);

#endif /* ATA_H */
