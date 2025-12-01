/* src/net/netdev.c - Network Device Abstraction Layer */
#include "netdev.h"
#include "../console.h"
#include "../pci.h"
#include "../drivers/pcnet.h"

/* Maximum de périphériques réseau supportés */
#define MAX_NETDEVS     4

/* Table des périphériques réseau */
static netdev_t netdevs[MAX_NETDEVS];
static int netdev_count_val = 0;

/* Périphérique par défaut */
static netdev_t* default_netdev = NULL;

/* ============================================ */
/*           Wrappers pour PCnet                */
/* ============================================ */

/**
 * Wrapper pour pcnet_send
 */
static bool pcnet_send_wrapper(netdev_t* dev, const uint8_t* data, uint16_t len)
{
    PCNetDevice* pcnet = (PCNetDevice*)dev->driver_data;
    if (pcnet != NULL) {
        bool result = pcnet_send(pcnet, data, len);
        if (result) {
            dev->packets_tx++;
        } else {
            dev->errors++;
        }
        return result;
    }
    return false;
}

/**
 * Wrapper pour pcnet_get_mac
 */
static void pcnet_get_mac_wrapper(netdev_t* dev, uint8_t* buf)
{
    (void)dev;  /* La MAC est stockée dans le netdev */
    PCNetDevice* pcnet = (PCNetDevice*)dev->driver_data;
    if (pcnet != NULL) {
        pcnet_get_mac(buf);
    }
}

/* ============================================ */
/*           Fonctions publiques                */
/* ============================================ */

/**
 * Initialise la couche d'abstraction réseau.
 */
int netdev_init(void)
{
    netdev_count_val = 0;
    default_netdev = NULL;
    
    /* Initialiser la table */
    for (int i = 0; i < MAX_NETDEVS; i++) {
        netdevs[i].initialized = false;
        netdevs[i].driver_data = NULL;
    }
    
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE);
    console_puts("[NETDEV] Detecting network devices...\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    
    /* === Détecter les cartes PCnet === */
    /* Vendor: AMD (0x1022), Device: PCnet-PCI II (0x2000) */
    PCIDevice* pci_dev = pci_get_device(0x1022, 0x2000);
    if (pci_dev != NULL) {
        PCNetDevice* pcnet = pcnet_init(pci_dev);
        if (pcnet != NULL && pcnet->initialized) {
            netdev_t* dev = &netdevs[netdev_count_val];
            
            dev->name = "eth0";
            dev->type = NETDEV_TYPE_PCNET;
            dev->driver_data = pcnet;
            dev->initialized = true;
            dev->packets_tx = 0;
            dev->packets_rx = 0;
            dev->errors = 0;
            
            /* Copier l'adresse MAC */
            pcnet_get_mac(dev->mac);
            
            /* Assigner les fonctions */
            dev->send = pcnet_send_wrapper;
            dev->get_mac = pcnet_get_mac_wrapper;
            
            /* Premier périphérique = par défaut */
            if (default_netdev == NULL) {
                default_netdev = dev;
            }
            
            netdev_count_val++;
            
            console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
            console_puts("[NETDEV] Found: ");
            console_puts(dev->name);
            console_puts(" (AMD PCnet-PCI II) MAC: ");
            for (int i = 0; i < 6; i++) {
                if (i > 0) console_putc(':');
                console_put_hex_byte(dev->mac[i]);
            }
            console_puts("\n");
            console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        }
    }
    
    /* === TODO: Détecter d'autres cartes ici === */
    /* RTL8139: Vendor 0x10EC, Device 0x8139 */
    /* E1000: Vendor 0x8086, Device 0x100E */
    /* VirtIO: Vendor 0x1AF4, Device 0x1000 */
    
    if (netdev_count_val == 0) {
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLUE);
        console_puts("[NETDEV] No network devices found!\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    } else {
        console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
        console_puts("[NETDEV] Total devices: ");
        console_put_dec(netdev_count_val);
        console_puts("\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    }
    
    return netdev_count_val;
}

/**
 * Retourne le périphérique réseau par défaut.
 */
netdev_t* netdev_get_default(void)
{
    return default_netdev;
}

/**
 * Retourne un périphérique réseau par son index.
 */
netdev_t* netdev_get(int index)
{
    if (index >= 0 && index < netdev_count_val) {
        return &netdevs[index];
    }
    return NULL;
}

/**
 * Envoie un paquet via le périphérique réseau par défaut.
 */
bool netdev_send(const uint8_t* data, uint16_t len)
{
    if (default_netdev != NULL && default_netdev->send != NULL) {
        return default_netdev->send(default_netdev, data, len);
    }
    return false;
}

/**
 * Copie l'adresse MAC du périphérique par défaut.
 */
void netdev_get_mac(uint8_t* buf)
{
    if (default_netdev != NULL) {
        for (int i = 0; i < 6; i++) {
            buf[i] = default_netdev->mac[i];
        }
    } else {
        /* MAC à zéro si pas de périphérique */
        for (int i = 0; i < 6; i++) {
            buf[i] = 0;
        }
    }
}

/**
 * Retourne le nombre de périphériques réseau disponibles.
 */
int netdev_count(void)
{
    return netdev_count_val;
}
