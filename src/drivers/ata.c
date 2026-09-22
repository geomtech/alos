/* src/drivers/ata.c - ATA PIO Mode Driver Implementation */
#include "ata.h"
#include "../arch/x86_64/io.h"
#include "../kernel/klog.h"
#include "../kernel/thread.h"

/* Flag indiquant si un disque a été détecté */
static int ata_disk_present = 0;

/* Lock pour protéger les accès concurrents au bus ATA */
static spinlock_t g_ata_lock;

/* Flag pour les interruptions (non utilisé en mode polling) */
static volatile int ata_irq_received = 0;

/**
 * Handler d'interruption ATA (IRQ 14).
 * Appelé depuis l'assembleur.
 */
void ata_irq_handler(void)
{
    /* Lire le status pour acquitter l'interruption */
    inb(ATA_PRIMARY_STATUS);
    ata_irq_received = 1;
}

/**
 * Attend que le contrôleur ATA ne soit plus occupé.
 * Boucle tant que le bit BSY est set dans le Status Register avec timeout.
 * @return 0 si succès, -1 si timeout
 */
int ata_wait_busy(void)
{
    /* On lit le status depuis le port Control pour ne pas clear l'IRQ */
    for (uint32_t i = 0; i < 500000; i++) {
        if (!(inb(ATA_PRIMARY_CONTROL) & ATA_SR_BSY)) {
            return 0;
        }
        io_wait();
    }
    KLOG_ERROR("ATA", "Timeout waiting for BSY to clear");
    return -1;
}

/**
 * Attend que le bit DRQ soit set (données prêtes à être lues/écrites).
 * Appelé après une commande READ/WRITE pour attendre les données.
 * @return 0 si succès, -1 si timeout ou erreur
 */
int ata_wait_drq(void)
{
    for (uint32_t i = 0; i < 500000; i++) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_ERR) {
            KLOG_ERROR("ATA", "Controller error while waiting for DRQ");
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            return 0;
        }
        io_wait();
    }
    KLOG_ERROR("ATA", "Timeout waiting for DRQ");
    return -1;
}

/**
 * Vérifie le status pour détecter une erreur.
 * @return 1 si erreur, 0 sinon
 */
static int ata_check_error(void)
{
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_SR_ERR) {
        return 1;
    }
    if (status & ATA_SR_DF) {
        return 1;  /* Device Fault */
    }
    return 0;
}

/**
 * Effectue un délai de 400ns en lisant le status 4 fois.
 * Nécessaire après certaines opérations ATA.
 */
static void ata_400ns_delay(void)
{
    /* Chaque lecture prend ~100ns */
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
}

/**
 * Vérifie si un disque est présent sur le bus Primary Master.
 */
int ata_is_present(void)
{
    return ata_disk_present;
}

/**
 * Initialise le contrôleur ATA et détecte les disques.
 */
int ata_init(void)
{
    KLOG_INFO("ATA", "Initializing ATA/IDE driver...");
    
    spinlock_init(&g_ata_lock);
    
    /* Sélectionner le Master drive */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    ata_400ns_delay();
    
    /* Soft reset du contrôleur */
    outb(ATA_PRIMARY_CONTROL, 0x04);  /* Set SRST */
    ata_400ns_delay();
    outb(ATA_PRIMARY_CONTROL, 0x00);  /* Clear SRST */
    ata_400ns_delay();
    
    /* Attendre que le contrôleur soit prêt */
    if (ata_wait_busy() != 0) {
        KLOG_WARN("ATA", "Timeout after reset on Primary Master");
        ata_disk_present = 0;
        return -1;
    }
    
    /* Vérifier si un disque est présent en lisant le status */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    
    /* Si status = 0xFF, aucun disque n'est connecté (bus flottant) */
    if (status == 0xFF) {
        KLOG_WARN("ATA", "No disk detected on Primary Master");
        ata_disk_present = 0;
        return -1;
    }
    
    /* Envoyer la commande IDENTIFY pour vérifier le type de périphérique */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    outb(ATA_PRIMARY_SECTOR_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LOW, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HIGH, 0);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    
    ata_400ns_delay();
    
    /* Vérifier la réponse */
    status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        KLOG_WARN("ATA", "No disk detected (IDENTIFY returned 0)");
        ata_disk_present = 0;
        return -1;
    }
    
    /* Attendre que BSY soit clear */
    if (ata_wait_busy() != 0) {
        KLOG_WARN("ATA", "Timeout waiting for BSY after IDENTIFY");
        ata_disk_present = 0;
        return -1;
    }
    
    /* Vérifier si c'est bien un disque ATA (LBA Mid et High doivent être 0) */
    uint8_t lba_mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t lba_high = inb(ATA_PRIMARY_LBA_HIGH);
    
    if (lba_mid != 0 || lba_high != 0) {
        KLOG_WARN("ATA", "Device is not ATA (ATAPI or SATA?)");
        /* On continue quand même, c'est peut-être un disque émulé */
    }
    
    /* Attendre DRQ ou ERR */
    if (ata_wait_drq() != 0) {
        KLOG_ERROR("ATA", "IDENTIFY command failed or timed out");
        ata_disk_present = 0;
        return -1;
    }
    
    /* Lire les 256 mots de données IDENTIFY (on les ignore pour l'instant) */
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ATA_PRIMARY_DATA);
    }
    
    /* Le disque est présent et fonctionnel */
    ata_disk_present = 1;
    
    KLOG_INFO("ATA", "Disk detected on Primary Master");
    
    /* Afficher quelques infos du disque */
    /* Word 60-61: Total sectors in LBA28 mode */
    uint32_t total_sectors = identify_data[60] | ((uint32_t)identify_data[61] << 16);
    uint32_t size_mb = total_sectors / 2048;  /* 512 bytes/sector, 1MB = 2048 sectors */
    
    KLOG_INFO_DEC("ATA", "Total sectors: ", total_sectors);
    KLOG_INFO_DEC("ATA", "Size (MB): ", size_mb);
    
    return 0;
}

/**
 * Lit des secteurs depuis le disque en mode PIO (LBA28).
 * Protégé par spinlock contre la concurrence multithread.
 * 
 * @param lba     Adresse LBA du premier secteur (0-based)
 * @param count   Nombre de secteurs à lire (1-255, 0 signifie 256)
 * @param buffer  Buffer de destination (doit pouvoir contenir count * 512 octets)
 * @return 0 si succès, -1 si erreur
 */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer)
{
    if (!ata_disk_present || buffer == NULL) {
        return -1;
    }
    
    uint64_t flags = spinlock_irqsave(&g_ata_lock);
    
    /* Attendre que le contrôleur soit prêt */
    if (ata_wait_busy() != 0) {
        spinlock_irqrestore(&g_ata_lock, flags);
        return -1;
    }
    
    /* Sélectionner le drive Master et envoyer les 4 bits hauts du LBA */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    
    /* Envoyer le nombre de secteurs à lire */
    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    
    /* Envoyer les 24 bits bas du LBA */
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)(lba & 0xFF));         /* Bits 0-7 */
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));  /* Bits 8-15 */
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF)); /* Bits 16-23 */
    
    /* Envoyer la commande READ PIO */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);
    
    /* Lire chaque secteur */
    uint8_t sectors_to_read = (count == 0) ? 256 : count;
    uint16_t* buf16 = (uint16_t*)buffer;
    
    for (int sector = 0; sector < sectors_to_read; sector++) {
        /* Petit délai de 400ns après la commande */
        ata_400ns_delay();
        
        /* Attendre que BSY soit clear */
        if (ata_wait_busy() != 0) {
            spinlock_irqrestore(&g_ata_lock, flags);
            return -1;
        }
        
        /* Vérifier les erreurs */
        if (ata_check_error()) {
            spinlock_irqrestore(&g_ata_lock, flags);
            return -1;
        }
        
        /* Attendre que DRQ soit set (données prêtes) */
        if (ata_wait_drq() != 0) {
            spinlock_irqrestore(&g_ata_lock, flags);
            return -1;
        }
        
        /* Lire 256 mots (512 octets) depuis le port Data */
        for (int i = 0; i < 256; i++) {
            buf16[sector * 256 + i] = inw(ATA_PRIMARY_DATA);
        }
    }
    
    spinlock_irqrestore(&g_ata_lock, flags);
    return 0;
}

/**
 * Écrit des secteurs sur le disque en mode PIO (LBA28).
 * Protégé par spinlock contre la concurrence multithread.
 * 
 * @param lba     Adresse LBA du premier secteur
 * @param count   Nombre de secteurs à écrire (1-255, 0 signifie 256)
 * @param buffer  Buffer source
 * @return 0 si succès, -1 si erreur
 */
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer)
{
    if (!ata_disk_present || buffer == NULL) {
        return -1;
    }
    
    uint64_t flags = spinlock_irqsave(&g_ata_lock);
    
    /* Attendre que le contrôleur soit prêt */
    if (ata_wait_busy() != 0) {
        spinlock_irqrestore(&g_ata_lock, flags);
        return -1;
    }
    
    /* Sélectionner le drive Master et envoyer les 4 bits hauts du LBA */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER | ((lba >> 24) & 0x0F));
    
    /* Envoyer le nombre de secteurs à écrire */
    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    
    /* Envoyer les 24 bits bas du LBA */
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Envoyer la commande WRITE PIO */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);
    
    /* Écrire chaque secteur */
    uint8_t sectors_to_write = (count == 0) ? 256 : count;
    const uint16_t* buf16 = (const uint16_t*)buffer;
    
    for (int sector = 0; sector < sectors_to_write; sector++) {
        /* Petit délai de 400ns */
        ata_400ns_delay();
        
        /* Attendre que BSY soit clear */
        if (ata_wait_busy() != 0) {
            spinlock_irqrestore(&g_ata_lock, flags);
            return -1;
        }
        
        /* Vérifier les erreurs */
        if (ata_check_error()) {
            spinlock_irqrestore(&g_ata_lock, flags);
            return -1;
        }
        
        /* Attendre que DRQ soit set */
        if (ata_wait_drq() != 0) {
            spinlock_irqrestore(&g_ata_lock, flags);
            return -1;
        }
        
        /* Écrire 256 mots (512 octets) sur le port Data */
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, buf16[sector * 256 + i]);
        }
    }
    
    /* Flush le cache du disque */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_busy();
    
    spinlock_irqrestore(&g_ata_lock, flags);
    return 0;
}

/**
 * Force l'écriture du cache disque sur le média.
 * Protégé par spinlock contre la concurrence multithread.
 * 
 * @return 0 si succès, -1 si erreur
 */
int ata_flush(void)
{
    if (!ata_disk_present) {
        return -1;
    }
    
    uint64_t flags = spinlock_irqsave(&g_ata_lock);
    
    /* Attendre que le contrôleur soit prêt */
    if (ata_wait_busy() != 0) {
        spinlock_irqrestore(&g_ata_lock, flags);
        return -1;
    }
    
    /* Sélectionner le drive Master */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    
    /* Envoyer la commande CACHE FLUSH */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    
    /* Attendre la fin du flush */
    if (ata_wait_busy() != 0) {
        spinlock_irqrestore(&g_ata_lock, flags);
        return -1;
    }
    
    /* Vérifier les erreurs */
    if (ata_check_error()) {
        spinlock_irqrestore(&g_ata_lock, flags);
        return -1;
    }
    
    spinlock_irqrestore(&g_ata_lock, flags);
    return 0;
}
