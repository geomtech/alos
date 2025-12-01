/* src/net/netdev.h - Network Device Abstraction Layer */
#ifndef NET_NETDEV_H
#define NET_NETDEV_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Types de drivers réseau supportés
 */
typedef enum {
    NETDEV_TYPE_NONE = 0,
    NETDEV_TYPE_PCNET,      /* AMD PCnet-PCI II */
    NETDEV_TYPE_RTL8139,    /* Realtek RTL8139 (futur) */
    NETDEV_TYPE_E1000,      /* Intel E1000 (futur) */
    NETDEV_TYPE_VIRTIO,     /* VirtIO Network (futur) */
} netdev_type_t;

/**
 * Flags pour l'état de l'interface réseau
 */
#define NETIF_FLAG_UP       (1 << 0)    /* Interface active */
#define NETIF_FLAG_DOWN     (1 << 1)    /* Interface inactive */
#define NETIF_FLAG_PROMISC  (1 << 2)    /* Mode promiscuous */
#define NETIF_FLAG_DHCP     (1 << 3)    /* Configuration DHCP */
#define NETIF_FLAG_RUNNING  (1 << 4)    /* Interface en cours d'exécution */

/* Forward declaration */
struct NetInterface;

/**
 * Structure d'interface réseau (style ipconfig/ifconfig)
 * 
 * Cette structure représente une interface réseau avec sa configuration
 * IP complète et son hook vers le driver hardware.
 */
typedef struct NetInterface {
    /* Identité de l'interface */
    char name[16];              /* Nom de l'interface (ex: "eth0") */
    uint8_t mac_addr[6];        /* Adresse MAC */
    
    /* Configuration IPv4 */
    uint32_t ip_addr;           /* Adresse IP (format host pour faciliter les masques) */
    uint32_t netmask;           /* Masque de sous-réseau */
    uint32_t gateway;           /* Passerelle par défaut */
    uint32_t dns_server;        /* Serveur DNS */
    
    /* État de l'interface */
    uint32_t flags;             /* Flags (UP, DOWN, PROMISC, etc.) */
    
    /* Driver hook - fonction d'envoi */
    int (*send)(struct NetInterface* self, uint8_t* data, int len);
    
    /* Données privées du driver */
    void* driver_data;
    
    /* Statistiques */
    uint32_t packets_tx;
    uint32_t packets_rx;
    uint32_t bytes_tx;
    uint32_t bytes_rx;
    uint32_t errors;
    
    /* Liste chaînée */
    struct NetInterface* next;
} NetInterface;

/* Ancienne structure pour compatibilité (sera supprimée progressivement) */
typedef struct netdev {
    /* Informations générales */
    const char*     name;           /* Nom du périphérique (ex: "eth0") */
    netdev_type_t   type;           /* Type de driver */
    uint8_t         mac[6];         /* Adresse MAC */
    bool            initialized;    /* État d'initialisation */
    void*           driver_data;    /* Données spécifiques au driver */
    
    /* Pointeurs vers les fonctions du driver */
    bool (*send)(struct netdev* dev, const uint8_t* data, uint16_t len);
    void (*get_mac)(struct netdev* dev, uint8_t* buf);
    
    /* Statistiques */
    uint32_t        packets_tx;
    uint32_t        packets_rx;
    uint32_t        errors;
} netdev_t;

/* ============================================ */
/*     API NetInterface (nouvelle API)          */
/* ============================================ */

/**
 * Enregistre une interface réseau dans la liste globale.
 * 
 * @param netif Interface à enregistrer
 */
void netdev_register(NetInterface* netif);

/**
 * Retourne l'interface réseau par défaut (première de la liste).
 * 
 * @return Pointeur vers la NetInterface ou NULL si aucune
 */
NetInterface* netif_get_default(void);

/**
 * Retourne une interface réseau par son nom.
 * 
 * @param name Nom de l'interface (ex: "eth0")
 * @return Pointeur vers la NetInterface ou NULL si non trouvée
 */
NetInterface* netif_get_by_name(const char* name);

/**
 * Affiche la configuration de toutes les interfaces (style ipconfig).
 */
void netdev_ipconfig_display(void);

/**
 * Convertit une adresse IP uint32_t en bytes (format network order).
 * 
 * @param ip_u32 Adresse IP en uint32_t
 * @param out    Buffer de 4 bytes pour le résultat
 */
void ip_u32_to_bytes(uint32_t ip_u32, uint8_t* out);

/**
 * Convertit une adresse IP bytes en uint32_t.
 * 
 * @param ip_bytes Adresse IP en 4 bytes
 * @return Adresse IP en uint32_t
 */
uint32_t ip_bytes_to_u32(const uint8_t* ip_bytes);

/**
 * Crée une adresse IP uint32_t à partir de 4 octets.
 * 
 * @param a Premier octet (ex: 10 pour 10.0.2.15)
 * @param b Deuxième octet
 * @param c Troisième octet
 * @param d Quatrième octet
 * @return Adresse IP en uint32_t
 */
#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                         ((uint32_t)(c) << 8) | (uint32_t)(d))

/* ============================================ */
/*     API Legacy (compatibilité)               */
/* ============================================ */

/**
 * Initialise la couche d'abstraction réseau.
 * Détecte et initialise les cartes réseau disponibles.
 * 
 * @return Nombre de périphériques réseau trouvés
 */
int netdev_init(void);

/**
 * Retourne le périphérique réseau par défaut (le premier trouvé).
 * 
 * @return Pointeur vers le netdev ou NULL si aucun n'est disponible
 */
netdev_t* netdev_get_default(void);

/**
 * Retourne un périphérique réseau par son index.
 * 
 * @param index Index du périphérique (0 = premier)
 * @return Pointeur vers le netdev ou NULL si l'index est invalide
 */
netdev_t* netdev_get(int index);

/**
 * Envoie un paquet via le périphérique réseau par défaut.
 * 
 * @param data Données à envoyer (frame Ethernet complète)
 * @param len  Longueur des données
 * @return true si succès, false sinon
 */
bool netdev_send(const uint8_t* data, uint16_t len);

/**
 * Copie l'adresse MAC du périphérique par défaut.
 * 
 * @param buf Buffer de 6 octets pour recevoir l'adresse MAC
 */
void netdev_get_mac(uint8_t* buf);

/**
 * Retourne le nombre de périphériques réseau disponibles.
 */
int netdev_count(void);

#endif /* NET_NETDEV_H */
