/* src/net/core/netdev.c - Network Device Abstraction Layer */
#include "netdev.h"
#include "../../drivers/net/pcnet.h"
#include "../../drivers/net/virtio_net.h"
#include "../../drivers/net/e1000e.h"
#include "../../drivers/pci.h"
#include "../../kernel/klog.h"
#include "../../kernel/console.h"

/* Maximum de périphériques réseau supportés */
#define MAX_NETDEVS 4

/* Table des périphériques réseau (legacy) */
static netdev_t netdevs[MAX_NETDEVS];
static int netdev_count_val = 0;

/* Périphérique par défaut (legacy) */
static netdev_t *default_netdev = NULL;

/* Liste chaînée des interfaces réseau (nouvelle API) */
static NetInterface *netif_list_head = NULL;
static int netif_count = 0;

/* ============================================ */
/*     Fonctions utilitaires IP                 */
/* ============================================ */

/**
 * Convertit une adresse IP uint32_t en bytes.
 */
void ip_u32_to_bytes(uint32_t ip_u32, uint8_t *out) {
  out[0] = (ip_u32 >> 24) & 0xFF;
  out[1] = (ip_u32 >> 16) & 0xFF;
  out[2] = (ip_u32 >> 8) & 0xFF;
  out[3] = ip_u32 & 0xFF;
}

/**
 * Convertit une adresse IP bytes en uint32_t.
 */
uint32_t ip_bytes_to_u32(const uint8_t *ip_bytes) {
  return ((uint32_t)ip_bytes[0] << 24) | ((uint32_t)ip_bytes[1] << 16) |
         ((uint32_t)ip_bytes[2] << 8) | (uint32_t)ip_bytes[3];
}

/* ============================================ */
/*     Nouvelle API NetInterface                */
/* ============================================ */

/**
 * Enregistre une interface réseau dans la liste globale.
 */
void netdev_register(NetInterface *netif) {
  if (netif == NULL)
    return;

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
    NetInterface *curr = netif_list_head;
    while (curr->next != NULL) {
      curr = curr->next;
    }
    curr->next = netif;
  }

  netif_count++;

  KLOG_INFO("NETIF", "Interface registered");
}

/**
 * Retourne l'interface réseau par défaut (première de la liste).
 */
NetInterface *netif_get_default(void) { return netif_list_head; }

/**
 * Retourne une interface réseau par son nom.
 */
NetInterface *netif_get_by_name(const char *name) {
  NetInterface *curr = netif_list_head;
  while (curr != NULL) {
    /* Comparaison simple de chaînes */
    const char *a = curr->name;
    const char *b = name;
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

/* Note: print_ip_u32 and print_mac_addr removed - using KLOG instead */

/**
 * Affiche un octet d'adresse IP en décimal.
 */
static void print_ip_byte(uint8_t byte) {
  console_put_dec(byte);
}

/**
 * Affiche une adresse IP uint32_t au format x.x.x.x.
 */
static void print_ip_u32(uint32_t ip) {
  uint8_t bytes[4];
  ip_u32_to_bytes(ip, bytes);
  print_ip_byte(bytes[0]);
  console_putc('.');
  print_ip_byte(bytes[1]);
  console_putc('.');
  print_ip_byte(bytes[2]);
  console_putc('.');
  print_ip_byte(bytes[3]);
}

/**
 * Affiche une adresse MAC au format XX:XX:XX:XX:XX:XX.
 */
static void print_mac_addr(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (i > 0) console_putc(':');
    console_put_hex_byte(mac[i]);
  }
}

/**
 * Affiche la configuration de toutes les interfaces (style ipconfig).
 * Note: Cette fonction utilise console_* directement car c'est une commande utilisateur.
 */
void netdev_ipconfig_display(void) {
  console_puts("\n");
  console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  console_puts("Network Configuration\n");
  console_puts("=====================\n\n");

  NetInterface *curr = netif_list_head;
  int count = 0;

  while (curr != NULL) {
    /* Nom de l'interface */
    console_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    console_puts(curr->name);
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts(":\n");

    /* Adresse MAC */
    console_puts("  MAC Address:    ");
    console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    print_mac_addr(curr->mac_addr);
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    console_puts("\n");

    /* Adresse IP */
    console_puts("  IPv4 Address:   ");
    if (curr->ip_addr != 0) {
      console_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
      print_ip_u32(curr->ip_addr);
      console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
      console_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
      console_puts("(not configured)");
      console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
    console_puts("\n");

    /* Masque de sous-réseau */
    console_puts("  Subnet Mask:    ");
    if (curr->netmask != 0) {
      print_ip_u32(curr->netmask);
    } else {
      console_puts("(not set)");
    }
    console_puts("\n");

    /* Passerelle */
    console_puts("  Gateway:        ");
    if (curr->gateway != 0) {
      print_ip_u32(curr->gateway);
    } else {
      console_puts("(not set)");
    }
    console_puts("\n");

    /* DNS */
    console_puts("  DNS Server:     ");
    if (curr->dns_server != 0) {
      print_ip_u32(curr->dns_server);
    } else {
      console_puts("(not set)");
    }
    console_puts("\n");

    /* Statistiques */
    console_puts("  Packets TX/RX:  ");
    console_put_dec(curr->packets_tx);
    console_puts(" / ");
    console_put_dec(curr->packets_rx);
    console_puts("\n");

    console_puts("\n");
    curr = curr->next;
    count++;
  }

  if (count == 0) {
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("No network interfaces found.\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  }
}

/* ============================================ */
/*           Wrappers pour PCnet                */
/* ============================================ */

/**
 * Wrapper pour pcnet_send
 */
static bool pcnet_send_wrapper(netdev_t *dev, const uint8_t *data,
                               uint16_t len) {
  PCNetDevice *pcnet = (PCNetDevice *)dev->driver_data;
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
static void pcnet_get_mac_wrapper(netdev_t *dev, uint8_t *buf) {
  (void)dev; /* La MAC est stockée dans le netdev */
  PCNetDevice *pcnet = (PCNetDevice *)dev->driver_data;
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
int netdev_init(void) {
  netdev_count_val = 0;
  default_netdev = NULL;

  /* Initialiser la table */
  for (int i = 0; i < MAX_NETDEVS; i++) {
    netdevs[i].initialized = false;
    netdevs[i].driver_data = NULL;
  }

  KLOG_INFO("NETDEV", "Detecting network devices...");

  /* === Détecter les cartes PCnet === */
  /* Vendor: AMD (0x1022), Device: PCnet-PCI II (0x2000) */
  PCIDevice *pci_dev = pci_get_device(0x1022, 0x2000);
  KLOG_INFO("NETDEV", "Found PCnet PCI device, initializing...");
  if (pci_dev != NULL) {
    KLOG_INFO("NETDEV", "Found PCnet PCI device, initializing...");
    PCNetDevice *pcnet = pcnet_init(pci_dev);
    /* Note: pcnet->initialized sera mis à true par pcnet_start() plus tard */
    if (pcnet != NULL) {
      netdev_t *dev = &netdevs[netdev_count_val];

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

      KLOG_INFO("NETDEV", "Found: AMD PCnet-PCI II");
    }
  }

  /* === Détecter les cartes Intel e1000/e1000e === */
  /* Vendor: Intel (0x8086), Device: 82540EM (0x100E) - QEMU default */
  KLOG_INFO("NETDEV", "Looking for Intel e1000...");
  PCIDevice *e1000_pci = pci_get_device(E1000E_VENDOR_ID, E1000E_DEV_82540EM);
  if (e1000_pci != NULL) {
    KLOG_INFO("NETDEV", "Found Intel e1000 PCI device, initializing...");
    E1000Device *e1000_dev = e1000e_init(e1000_pci);
    if (e1000_dev != NULL) {
      /* Add to legacy array for compatibility */
      if (netdev_count_val < MAX_NETDEVS) {
        netdev_t *dev = &netdevs[netdev_count_val];

        /* Dynamic naming */
        if (netdev_count_val == 0) {
          dev->name = "eth0";
        } else if (netdev_count_val == 1) {
          dev->name = "eth1";
        } else {
          dev->name = "eth2";
        }

        dev->type = NETDEV_TYPE_E1000;
        dev->driver_data = e1000_dev;
        dev->initialized = true;
        dev->packets_tx = 0;
        dev->packets_rx = 0;
        dev->errors = 0;

        /* Copy MAC */
        e1000e_get_mac(e1000_dev, dev->mac);

        /* Set as default if none yet */
        if (default_netdev == NULL) {
          default_netdev = dev;
        }

        netdev_count_val++;
      }

      KLOG_INFO("NETDEV", "Found: Intel e1000 Network Device");
    }
  }

  /* === Détecter les cartes VirtIO === */
  /* VirtIO: Vendor 0x1AF4, Device 0x1000 */
  KLOG_INFO("NETDEV", "Looking for VirtIO...");
  PCIDevice *virtio_pci = pci_get_device(0x1AF4, 0x1000);
  if (virtio_pci != NULL) {
    KLOG_INFO("NETDEV", "Found VirtIO PCI device, initializing...");
    VirtIONetDevice *virtio_dev = virtio_net_init(virtio_pci);
    if (virtio_dev != NULL) {
      /* Note: netdev registration is handled inside virtio_net_init for now to
       * match pcnet style */
      /* But we should probably unify this logic later. */
      /* For now, just increment count if we want to track it here, but
       * netdev_register does it too */
      /* Actually, netdev_register increments netif_count, but netdev_count_val
       * is for legacy array */

      /* Add to legacy array for compatibility if needed */
      if (netdev_count_val < MAX_NETDEVS) {
        netdev_t *dev = &netdevs[netdev_count_val];

        /* Dynamic naming */
        if (netdev_count_val == 0) {
          dev->name = "eth0";
        } else {
          dev->name = "eth1"; /* TODO: proper snprintf */
        }

        dev->type = NETDEV_TYPE_VIRTIO;
        dev->driver_data = virtio_dev;
        dev->initialized = true;

        /* Copy MAC */
        for (int i = 0; i < 6; i++)
          dev->mac[i] = virtio_dev->mac_addr[i];

        /* Set as default if none yet */
        if (default_netdev == NULL) {
          default_netdev = dev;
        }

        netdev_count_val++;
      }

      KLOG_INFO("NETDEV", "Found: Virtio Network Device");
    }
  }

  if (netdev_count_val == 0) {
    KLOG_ERROR("NETDEV", "No network devices found!");
  } else {
    KLOG_INFO_DEC("NETDEV", "Total devices: ", netdev_count_val);
  }

  return netdev_count_val;
}

/**
 * Retourne le périphérique réseau par défaut.
 */
netdev_t *netdev_get_default(void) { return default_netdev; }

/**
 * Retourne un périphérique réseau par son index.
 */
netdev_t *netdev_get(int index) {
  if (index >= 0 && index < netdev_count_val) {
    return &netdevs[index];
  }
  return NULL;
}

/**
 * Envoie un paquet via le périphérique réseau par défaut.
 */
bool netdev_send(const uint8_t *data, uint16_t len) {
  if (default_netdev != NULL && default_netdev->send != NULL) {
    return default_netdev->send(default_netdev, data, len);
  }
  return false;
}

/**
 * Copie l'adresse MAC du périphérique par défaut.
 */
void netdev_get_mac(uint8_t *buf) {
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
int netdev_count(void) { return netdev_count_val; }

/**
 * Handler d'interruption réseau global (IRQ 11).
 * Dispatche vers les drivers appropriés.
 */
void network_irq_handler(void) {
  /* Poll Virtio */
  VirtIONetDevice *virtio_dev = virtio_net_get_device();
  if (virtio_dev != NULL) {
    virtio_net_poll();
  }

  /* Poll PCnet */
  PCNetDevice *pcnet_dev = pcnet_get_device();
  if (pcnet_dev != NULL) {
    pcnet_irq_handler();
  }

  /* Poll e1000e */
  E1000Device *e1000_dev = e1000e_get_device();
  if (e1000_dev != NULL) {
    e1000e_poll();
  }
}
