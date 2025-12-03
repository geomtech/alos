/* src/net/core/netdev.c - Network Device Abstraction Layer */
#include "netdev.h"
#include "../netlog.h"
#include "../../drivers/pci.h"
#include "../../drivers/net/pcnet.h"

/* Maximum de périphériques réseau supportés */
#define MAX_NETDEVS     4

/* Table des périphériques réseau (legacy) */
static netdev_t netdevs[MAX_NETDEVS];
static int netdev_count_val = 0;

/* Périphérique par défaut (legacy) */
static netdev_t* default_netdev = NULL;

/* Liste chaînée des interfaces réseau (nouvelle API) */
static NetInterface* netif_list_head = NULL;
static int netif_count = 0;

/* ============================================ */
/*     Fonctions utilitaires IP                 */
/* ============================================ */

/**
 * Convertit une adresse IP uint32_t en bytes.
 */
void ip_u32_to_bytes(uint32_t ip_u32, uint8_t* out)
{
    out[0] = (ip_u32 >> 24) & 0xFF;
    out[1] = (ip_u32 >> 16) & 0xFF;
    out[2] = (ip_u32 >> 8) & 0xFF;
    out[3] = ip_u32 & 0xFF;
}

/**
 * Convertit une adresse IP bytes en uint32_t.
 */
uint32_t ip_bytes_to_u32(const uint8_t* ip_bytes)
{
    return ((uint32_t)ip_bytes[0] << 24) |
           ((uint32_t)ip_bytes[1] << 16) |
           ((uint32_t)ip_bytes[2] << 8) |
           (uint32_t)ip_bytes[3];
}

/* ============================================ */
/*     Nouvelle API NetInterface                */
/* ============================================ */

/**
 * Enregistre une interface réseau dans la liste globale.
 */
void netdev_register(NetInterface* netif)
{
    if (netif == NULL) return;
    
    /* Initialiser les statistiques */
    netif->packets_tx = 0;
    netif->packets_rx = 0;
    netif->bytes_tx = 0;
    netif->bytes_rx = 0;
    netif->errors = 0;
    netif->next = NULL;
    
    /* Ajouter en tête de liste */
    if (netif_list_head == NULL) {
        netif_list_head = netif;
    } else {
        /* Ajouter à la fin */
        NetInterface* curr = netif_list_head;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = netif;
    }
    
    netif_count++;
    
    net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    net_puts("[NETIF] Registered: ");
    net_puts(netif->name);
    net_puts(" (MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) net_putc(':');
        net_put_hex_byte(netif->mac_addr[i]);
    }
    net_puts(")\n");
    net_reset_color();
}

/**
 * Retourne l'interface réseau par défaut (première de la liste).
 */
NetInterface* netif_get_default(void)
{
    return netif_list_head;
}

/**
 * Retourne une interface réseau par son nom.
 */
NetInterface* netif_get_by_name(const char* name)
{
    NetInterface* curr = netif_list_head;
    while (curr != NULL) {
        /* Comparaison simple de chaînes */
        const char* a = curr->name;
        const char* b = name;
        bool match = true;
        while (*a && *b) {
            if (*a++ != *b++) {
                match = false;
                break;
            }
        }
        if (match && *a == *b) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

/**
 * Affiche une adresse IP au format x.x.x.x (depuis uint32_t).
 */
static void print_ip_u32(uint32_t ip)
{
    uint8_t bytes[4];
    ip_u32_to_bytes(ip, bytes);
    for (int i = 0; i < 4; i++) {
        if (i > 0) net_putc('.');
        net_put_dec(bytes[i]);
    }
}

/**
 * Affiche une adresse MAC au format XX:XX:XX:XX:XX:XX.
 */
static void print_mac_addr(const uint8_t* mac)
{
    for (int i = 0; i < 6; i++) {
        if (i > 0) net_putc(':');
        net_put_hex_byte(mac[i]);
    }
}

/**
 * Affiche la configuration de toutes les interfaces (style ipconfig).
 */
void netdev_ipconfig_display(void)
{
    net_puts("\n");
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("===============================================\n");
    net_puts("        Network Interface Configuration\n");
    net_puts("===============================================\n\n");
    net_reset_color();
    
    if (netif_list_head == NULL) {
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts("  No network interfaces found.\n");
        net_reset_color();
        return;
    }
    
    NetInterface* netif = netif_list_head;
    int index = 0;
    
    while (netif != NULL) {
        /* Nom de l'interface */
        net_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        net_puts("Interface ");
        net_put_dec(index);
        net_puts(": ");
        net_puts(netif->name);
        net_puts("\n");
        net_reset_color();
        
        /* Adresse MAC */
        net_puts("   Physical Address . . . : ");
        net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        print_mac_addr(netif->mac_addr);
        net_puts("\n");
        net_reset_color();
        
        /* Adresse IPv4 */
        net_puts("   IPv4 Address . . . . . : ");
        if (netif->ip_addr != 0) {
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            print_ip_u32(netif->ip_addr);
        } else {
            net_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            net_puts("Not configured");
        }
        net_puts("\n");
        net_reset_color();
        
        /* Masque de sous-réseau */
        net_puts("   Subnet Mask  . . . . . : ");
        if (netif->netmask != 0) {
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            print_ip_u32(netif->netmask);
        } else {
            net_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            net_puts("Not configured");
        }
        net_puts("\n");
        net_reset_color();
        
        /* Passerelle */
        net_puts("   Default Gateway  . . . : ");
        if (netif->gateway != 0) {
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            print_ip_u32(netif->gateway);
        } else {
            net_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            net_puts("Not configured");
        }
        net_puts("\n");
        net_reset_color();
        
        /* DNS Server */
        net_puts("   DNS Server . . . . . . : ");
        if (netif->dns_server != 0) {
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            print_ip_u32(netif->dns_server);
        } else {
            net_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            net_puts("Not configured");
        }
        net_puts("\n");
        net_reset_color();
        
        /* État */
        net_puts("   Status . . . . . . . . : ");
        if (netif->flags & NETIF_FLAG_UP) {
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            net_puts("UP");
        } else {
            net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            net_puts("DOWN");
        }
        if (netif->flags & NETIF_FLAG_RUNNING) {
            net_puts(" RUNNING");
        }
        if (netif->flags & NETIF_FLAG_PROMISC) {
            net_puts(" PROMISC");
        }
        if (netif->flags & NETIF_FLAG_DHCP) {
            net_puts(" DHCP");
        }
        net_puts("\n");
        net_reset_color();
        
        /* Statistiques */
        net_puts("   TX Packets . . . . . . : ");
        net_put_dec(netif->packets_tx);
        net_puts("  RX Packets: ");
        net_put_dec(netif->packets_rx);
        net_puts("  Errors: ");
        net_put_dec(netif->errors);
        net_puts("\n");
        
        net_puts("\n");
        
        netif = netif->next;
        index++;
    }
    
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("===============================================\n");
    net_reset_color();
}

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
    
    net_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    net_puts("[NETDEV] Detecting network devices...\n");
    net_reset_color();
    
    /* === Détecter les cartes PCnet === */
    /* Vendor: AMD (0x1022), Device: PCnet-PCI II (0x2000) */
    PCIDevice* pci_dev = pci_get_device(0x1022, 0x2000);
    if (pci_dev != NULL) {
        PCNetDevice* pcnet = pcnet_init(pci_dev);
        /* Note: pcnet->initialized sera mis à true par pcnet_start() plus tard */
        if (pcnet != NULL) {
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
            
            net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            net_puts("[NETDEV] Found: ");
            net_puts(dev->name);
            net_puts(" (AMD PCnet-PCI II) MAC: ");
            for (int i = 0; i < 6; i++) {
                if (i > 0) net_putc(':');
                net_put_hex_byte(dev->mac[i]);
            }
            net_puts("\n");
            net_reset_color();
        }
    }
    
    /* === TODO: Détecter d'autres cartes ici === */
    /* RTL8139: Vendor 0x10EC, Device 0x8139 */
    /* E1000: Vendor 0x8086, Device 0x100E */
    /* VirtIO: Vendor 0x1AF4, Device 0x1000 */
    
    if (netdev_count_val == 0) {
        net_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        net_puts("[NETDEV] No network devices found!\n");
        net_reset_color();
    } else {
        net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        net_puts("[NETDEV] Total devices: ");
        net_put_dec(netdev_count_val);
        net_puts("\n");
        net_reset_color();
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
