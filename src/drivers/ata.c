/* src/drivers/ata.c - ATA PIO Mode Driver Implementation */
#include "ata.h"
#include "../arch/x86/io.h"
#include "../kernel/console.h"

/* Flag indiquant si un disque a été détecté */
static int ata_disk_present = 0;

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
 * Boucle tant que le bit BSY est set dans le Status Register.
 */
void ata_wait_busy(void)
{
    /* On lit le status depuis le port Control pour ne pas clear l'IRQ */
    while (inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) {
        /* Busy loop - le contrôleur traite une commande */
    }
}

/**
 * Attend que le bit DRQ soit set (données prêtes à être lues/écrites).
 * Appelé après une commande READ/WRITE pour attendre les données.
 */
void ata_wait_drq(void)
{
    while (!(inb(ATA_PRIMARY_STATUS) & ATA_SR_DRQ)) {
        /* Attente des données */
    }
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
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("\n=== ATA/IDE Driver ===\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Sélectionner le Master drive */
    outb(ATA_PRIMARY_DRIVE_HEAD, ATA_DRIVE_MASTER);
    ata_400ns_delay();
    
    /* Soft reset du contrôleur */
    outb(ATA_PRIMARY_CONTROL, 0x04);  /* Set SRST */
    ata_400ns_delay();
    outb(ATA_PRIMARY_CONTROL, 0x00);  /* Clear SRST */
    ata_400ns_delay();
    
    /* Attendre que le contrôleur soit prêt */
    ata_wait_busy();
    
    /* Vérifier si un disque est présent en lisant le status */
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    
    /* Si status = 0xFF, aucun disque n'est connecté (bus flottant) */
    if (status == 0xFF) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[ATA] No disk detected on Primary Master\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
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
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[ATA] No disk detected (IDENTIFY returned 0)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        ata_disk_present = 0;
        return -1;
    }
    
    /* Attendre que BSY soit clear */
    ata_wait_busy();
    
    /* Vérifier si c'est bien un disque ATA (LBA Mid et High doivent être 0) */
    uint8_t lba_mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t lba_high = inb(ATA_PRIMARY_LBA_HIGH);
    
    if (lba_mid != 0 || lba_high != 0) {
        console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE);
        console_puts("[ATA] Device is not ATA (ATAPI or SATA?)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        /* On continue quand même, c'est peut-être un disque émulé */
    }
    
    /* Attendre DRQ ou ERR */
    while (1) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_ERR) {
            console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
            console_puts("[ATA] IDENTIFY command failed\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
            ata_disk_present = 0;
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            break;  /* Données prêtes */
        }
    }
    
    /* Lire les 256 mots de données IDENTIFY (on les ignore pour l'instant) */
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ATA_PRIMARY_DATA);
    }
    
    /* Le disque est présent et fonctionnel */
    ata_disk_present = 1;
    
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[ATA] Disk detected on Primary Master\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* Afficher quelques infos du disque */
    /* Word 60-61: Total sectors in LBA28 mode */
    uint32_t total_sectors = identify_data[60] | ((uint32_t)identify_data[61] << 16);
    uint32_t size_mb = total_sectors / 2048;  /* 512 bytes/sector, 1MB = 2048 sectors */
    
    console_puts("[ATA] Total sectors: ");
    console_put_dec(total_sectors);
    console_puts(" (~");
    console_put_dec(size_mb);
    console_puts(" MB)\n");
    
    return 0;
}

/**
 * Lit des secteurs depuis le disque en mode PIO (LBA28).
 * 
 * @param lba     Adresse LBA du premier secteur (0-based)
 * @param count   Nombre de secteurs à lire (1-255, 0 signifie 256)
 * @param buffer  Buffer de destination (doit pouvoir contenir count * 512 octets)
 * @return 0 si succès, -1 si erreur
 */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer)
{
    if (!ata_disk_present) {
        return -1;
    }
    
    if (buffer == NULL) {
        return -1;
    }
    
    /* Attendre que le contrôleur soit prêt */
    ata_wait_busy();
    
    /* Sélectionner le drive Master et envoyer les 4 bits hauts du LBA */
    /* Format: 1110 XXXX où XXXX = bits 24-27 du LBA */
    /* Le bit 6 (0x40) active le mode LBA */
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
        ata_wait_busy();
        
        /* Vérifier les erreurs */
        if (ata_check_error()) {
            return -1;
        }
        
        /* Attendre que DRQ soit set (données prêtes) */
        ata_wait_drq();
        
        /* Lire 256 mots (512 octets) depuis le port Data */
        /* Note: On lit des mots de 16 bits (inw) car l'ATA fonctionne en 16-bit */
        for (int i = 0; i < 256; i++) {
            buf16[sector * 256 + i] = inw(ATA_PRIMARY_DATA);
        }
    }
    
    return 0;
}

/**
 * Écrit des secteurs sur le disque en mode PIO (LBA28).
 * 
 * @param lba     Adresse LBA du premier secteur
 * @param count   Nombre de secteurs à écrire (1-255, 0 signifie 256)
 * @param buffer  Buffer source
 * @return 0 si succès, -1 si erreur
 */
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer)
{
    if (!ata_disk_present) {
        return -1;
    }
    
    if (buffer == NULL) {
        return -1;
    }
    
    /* Attendre que le contrôleur soit prêt */
    ata_wait_busy();
    
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
        ata_wait_busy();
        
        /* Vérifier les erreurs */
        if (ata_check_error()) {
            return -1;
        }
        
        /* Attendre que DRQ soit set */
        ata_wait_drq();
        
        /* Écrire 256 mots (512 octets) sur le port Data */
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, buf16[sector * 256 + i]);
        }
    }
    
    /* Flush le cache du disque */
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_busy();
    
    return 0;
}
