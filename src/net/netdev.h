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
 * Structure abstraite pour un périphérique réseau.
 * Contient des pointeurs vers les fonctions du driver spécifique.
 */
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
