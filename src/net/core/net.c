/* src/net/core/net.c - Network Configuration */
#include "net.h"
#include "netdev.h"
#include "../../kernel/console.h"
#include "../../drivers/net/pcnet.h"

/* 
 * Variables globales LEGACY - DEPRECATED
 * Ces variables sont conservées pour compatibilité mais seront supprimées.
 * La vraie configuration est maintenant dans NetInterface.
 * 
 * Pour DHCP: l'IP sera 0.0.0.0 jusqu'à réception d'un bail.
 */
uint8_t MY_IP[4] = {0, 0, 0, 0};      /* Pas d'IP statique par défaut */
uint8_t MY_MAC[6] = {0, 0, 0, 0, 0, 0};
uint8_t GATEWAY_IP[4] = {0, 0, 0, 0}; /* Sera configuré par DHCP */
uint8_t DNS_IP[4] = {0, 0, 0, 0};     /* Sera configuré par DHCP */
uint8_t NETMASK[4] = {0, 0, 0, 0};    /* Sera configuré par DHCP */

/**
 * Initialise les paramètres réseau.
 * Note: La vraie configuration IP est dans NetInterface et sera faite par DHCP.
 */
void net_init(uint8_t* mac)
{
    /* Copier notre adresse MAC dans la variable legacy */
    for (int i = 0; i < 6; i++) {
        MY_MAC[i] = mac[i];
    }
    
    /* Afficher l'état initial (en attente de DHCP) */
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_puts("[NET] Network layer initialized\n");
    console_puts("      MAC:     ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) console_putc(':');
        console_put_hex_byte(MY_MAC[i]);
    }
    console_puts("\n      Status:  Waiting for DHCP or static configuration\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/**
 * Compare deux adresses IP.
 */
int ip_equals(const uint8_t* ip1, const uint8_t* ip2)
{
    for (int i = 0; i < 4; i++) {
        if (ip1[i] != ip2[i]) return 0;
    }
    return 1;
}

/**
 * Compare deux adresses MAC.
 */
int mac_equals(const uint8_t* mac1, const uint8_t* mac2)
{
    for (int i = 0; i < 6; i++) {
        if (mac1[i] != mac2[i]) return 0;
    }
    return 1;
}

/**
 * Vérifie si une adresse MAC est broadcast.
 */
int mac_is_broadcast(const uint8_t* mac)
{
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) return 0;
    }
    return 1;
}

/**
 * Traite les paquets réseau en attente (polling mode).
 * Cette fonction appelle le handler d'interruption PCNet
 * pour traiter les paquets reçus.
 */
void net_poll(void)
{
    /* Appeler le handler d'interruption pour traiter les paquets */
    pcnet_irq_handler();
}
