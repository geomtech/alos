/* src/net/core/net.c - Network Configuration */
#include "net.h"
#include "../../kernel/console.h"

/* Notre adresse IP (10.0.2.15 - IP par défaut dans QEMU SLIRP) */
uint8_t MY_IP[4] = {10, 0, 2, 15};

/* Notre adresse MAC (initialisée à zéro, sera remplie par net_init) */
uint8_t MY_MAC[6] = {0, 0, 0, 0, 0, 0};

/* Adresse de la gateway QEMU (10.0.2.2) */
uint8_t GATEWAY_IP[4] = {10, 0, 2, 2};

/* Adresse du serveur DNS QEMU (10.0.2.3) */
uint8_t DNS_IP[4] = {10, 0, 2, 3};

/* Masque de sous-réseau (255.255.255.0) */
uint8_t NETMASK[4] = {255, 255, 255, 0};

/**
 * Initialise les paramètres réseau.
 */
void net_init(uint8_t* mac)
{
    /* Copier notre adresse MAC */
    for (int i = 0; i < 6; i++) {
        MY_MAC[i] = mac[i];
    }
    
    /* Afficher la configuration */
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE);
    console_puts("[NET] Network identity configured:\n");
    console_puts("      IP:      ");
    for (int i = 0; i < 4; i++) {
        if (i > 0) console_putc('.');
        console_put_dec(MY_IP[i]);
    }
    console_puts("\n      MAC:     ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) console_putc(':');
        console_put_hex_byte(MY_MAC[i]);
    }
    console_puts("\n      Gateway: ");
    for (int i = 0; i < 4; i++) {
        if (i > 0) console_putc('.');
        console_put_dec(GATEWAY_IP[i]);
    }
    console_puts("\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
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
