/* src/drivers/net/virtio_net.c - VirtIO Network Driver
 *
 * Ce driver utilise l'abstraction de transport VirtIO qui supporte:
 * - PCI PIO (Port I/O) : Mode legacy
 * - PCI MMIO : Mode moderne via BAR MMIO (auto-détecté)
 * - MMIO natif : Pour systèmes sans PCI
 *
 * Le mode MMIO est automatiquement sélectionné si un BAR MMIO est disponible.
 */

#include "virtio_net.h"
#include "../virtio/virtio_transport.h"
#include "../../arch/x86/idt.h"
#include "../../arch/x86/io.h"
#include "../../mm/kheap.h"
#include "../../net/core/netdev.h"
#include "../../net/l2/ethernet.h"
#include "../../net/netlog.h"
#include "../../kernel/klog.h"

/* ============================================ */
/*           Structures internes                */
/* ============================================ */

/* Structure interne du driver réseau VirtIO */
typedef struct {
    VirtioDevice *vdev;         /* Device VirtIO abstrait */
    VirtQueue rx_queue;         /* Queue de réception */
    VirtQueue tx_queue;         /* Queue de transmission */
    
    uint8_t mac_addr[6];        /* Adresse MAC */
    bool initialized;
    
    /* Statistiques */
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t errors;
} VirtioNetDriver;

/* Header VirtIO-Net (doit précéder chaque paquet)
 * Taille: 10 bytes pour Legacy, 12 bytes si VIRTIO_NET_F_MRG_RXBUF
 */
typedef struct {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    /* num_buffers n'est PAS inclus en mode Legacy sans MRG_RXBUF */
} __attribute__((packed)) VirtioNetHdr;

#define VIRTIO_NET_HDR_SIZE 10  /* Taille du header Legacy */

/* ============================================ */
/*           Variables globales                 */
/* ============================================ */

static VirtioNetDriver *g_driver = NULL;
static NetInterface *g_netif = NULL;

/* Taille des buffers RX */
#define RX_BUFFER_SIZE 2048
#define RX_BUFFER_COUNT 16

/* Buffers RX pré-alloués */
static uint8_t *rx_buffers[RX_BUFFER_COUNT];

/* ============================================ */
/*           Fonctions internes                 */
/* ============================================ */

/**
 * Remplit la queue RX avec des buffers.
 */
static void virtio_net_refill_rx(VirtioNetDriver *drv) {
    VirtQueue *vq = &drv->rx_queue;
    
    for (int i = 0; i < RX_BUFFER_COUNT && vq->num_free >= 1; i++) {
        if (rx_buffers[i] == NULL) {
            rx_buffers[i] = (uint8_t *)kmalloc(RX_BUFFER_SIZE);
            if (rx_buffers[i] == NULL) {
                continue;
            }
        }
        
        /* Ajouter le buffer à la queue (device-writable) */
        int idx = virtio_queue_add_buf(vq, rx_buffers[i], RX_BUFFER_SIZE, true, false);
        if (idx < 0) {
            break;
        }
    }
    
    /* Notifier le device */
    virtio_notify(drv->vdev, vq);
}

/**
 * Traite les paquets reçus.
 */
static void virtio_net_receive(VirtioNetDriver *drv) {
    VirtQueue *vq = &drv->rx_queue;
    
    while (virtio_queue_has_used(vq)) {
        uint32_t len;
        uint8_t *buf = (uint8_t *)virtio_queue_get_used(vq, &len);
        
        if (buf == NULL || len <= VIRTIO_NET_HDR_SIZE) {
            continue;
        }
        
        /* Le paquet commence après le header VirtIO (10 bytes) */
        uint8_t *pkt_data = buf + VIRTIO_NET_HDR_SIZE;
        uint32_t pkt_len = len - VIRTIO_NET_HDR_SIZE;
        
        /* Traiter le paquet Ethernet */
        if (pkt_len > 0 && g_netif != NULL) {
            ethernet_handle_packet_netif(g_netif, pkt_data, pkt_len);
            drv->packets_rx++;
            
            if (g_netif != NULL) {
                g_netif->packets_rx++;
                g_netif->bytes_rx += pkt_len;
            }
        }
        
        /* Remettre le buffer dans la queue RX */
        virtio_queue_add_buf(vq, buf, RX_BUFFER_SIZE, true, false);
    }
    
    /* Notifier le device */
    virtio_notify(drv->vdev, vq);
}

/**
 * Handler d'interruption.
 */
static void virtio_net_irq_handler_internal(void) {
    if (g_driver == NULL || g_driver->vdev == NULL) {
        return;
    }
    
    if (g_driver->vdev->ops == NULL || g_driver->vdev->ops->ack_interrupt == NULL) {
        return;
    }
    
    /* Acquitter l'interruption */
    uint32_t isr = g_driver->vdev->ops->ack_interrupt(g_driver->vdev);
    
    if (isr & 1) {
        /* Queue interrupt - traiter les paquets */
        virtio_net_receive(g_driver);
    }
    
    if (isr & 2) {
        /* Config change interrupt - ignoré pour l'instant */
    }
}

/* Handler d'IRQ exporté */
void virtio_net_irq_handler(void) {
    virtio_net_irq_handler_internal();
    
    /* EOI */
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

/**
 * Fonction d'envoi pour NetInterface.
 */
static int virtio_netif_send(NetInterface *netif, uint8_t *data, int len) {
    if (netif == NULL || netif->driver_data == NULL) {
        return -1;
    }
    
    VirtioNetDriver *drv = (VirtioNetDriver *)netif->driver_data;
    
    if (!drv->initialized || drv->vdev == NULL) {
        return -1;
    }
    
    VirtQueue *vq = &drv->tx_queue;
    
    /* Vérifier qu'on a au moins un descriptor libre */
    if (vq->num_free < 1) {
        /* Essayer de récupérer les buffers utilisés */
        while (virtio_queue_has_used(vq)) {
            uint32_t used_len;
            void *used_buf = virtio_queue_get_used(vq, &used_len);
            if (used_buf != NULL) {
                kfree(used_buf);
            }
        }
        
        if (vq->num_free < 1) {
            drv->errors++;
            return -1;
        }
    }
    
    /* Allouer le header + données dans un seul buffer */
    uint32_t total_len = VIRTIO_NET_HDR_SIZE + len;
    uint8_t *buf = (uint8_t *)kmalloc(total_len);
    if (buf == NULL) {
        drv->errors++;
        return -1;
    }
    
    /* Remplir le header (10 bytes) */
    VirtioNetHdr *hdr = (VirtioNetHdr *)buf;
    hdr->flags = 0;
    hdr->gso_type = 0;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;
    
    /* Copier les données après le header */
    uint8_t *pkt_data = buf + VIRTIO_NET_HDR_SIZE;
    for (int i = 0; i < len; i++) {
        pkt_data[i] = data[i];
    }
    
    /* Ajouter à la queue TX */
    int idx = virtio_queue_add_buf(vq, buf, total_len, false, false);
    if (idx < 0) {
        kfree(buf);
        drv->errors++;
        return -1;
    }
    
    /* Notifier le device */
    virtio_notify(drv->vdev, vq);
    
    drv->packets_tx++;
    if (netif != NULL) {
        netif->packets_tx++;
        netif->bytes_tx += len;
    }
    
    return len;
}

/* ============================================ */
/*           API publique                       */
/* ============================================ */

VirtIONetDevice *virtio_net_init(PCIDevice *pci_dev) {
    net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    net_puts("\n=== VirtIO Network Driver (MMIO) ===\n");
    net_reset_color();
    
    if (pci_dev == NULL) {
        return NULL;
    }
    
    /* Créer le device VirtIO via l'abstraction de transport */
    VirtioDevice *vdev = virtio_create_from_pci(pci_dev);
    if (vdev == NULL) {
        net_puts("[VirtIO-Net] Failed to create VirtIO device\n");
        return NULL;
    }
    
    /* Afficher le mode de transport
     * Note: VirtIO PCI Legacy utilise toujours PIO car BAR0 est un I/O BAR.
     * Le BAR1 MMIO est pour MSI-X, pas pour les registres.
     */
    net_puts("[VirtIO-Net] Transport: PCI Legacy (PIO)\n");
    net_puts("[VirtIO-Net] I/O Base: 0x");
    net_put_hex(vdev->transport.pci.io_base);
    net_puts("\n");
    
    /* Allouer le driver */
    VirtioNetDriver *drv = (VirtioNetDriver *)kmalloc(sizeof(VirtioNetDriver));
    if (drv == NULL) {
        virtio_destroy(vdev);
        return NULL;
    }
    
    drv->vdev = vdev;
    drv->initialized = false;
    drv->packets_rx = 0;
    drv->packets_tx = 0;
    drv->errors = 0;
    
    /* Initialiser les buffers RX */
    for (int i = 0; i < RX_BUFFER_COUNT; i++) {
        rx_buffers[i] = NULL;
    }
    
    /* Enable Bus Mastering */
    pci_enable_bus_mastering(pci_dev);
    
    /* Initialiser le device VirtIO */
    uint32_t required_features = VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS;
    if (virtio_init_device(vdev, required_features) < 0) {
        net_puts("[VirtIO-Net] Device initialization failed!\n");
        kfree(drv);
        virtio_destroy(vdev);
        return NULL;
    }
    
    /* Configurer les queues */
    net_puts("[VirtIO-Net] Setting up RX queue...\n");
    if (virtio_setup_queue(vdev, &drv->rx_queue, 0) < 0) {
        net_puts("[VirtIO-Net] RX queue setup failed!\n");
        kfree(drv);
        virtio_destroy(vdev);
        return NULL;
    }
    
    net_puts("[VirtIO-Net] Setting up TX queue...\n");
    if (virtio_setup_queue(vdev, &drv->tx_queue, 1) < 0) {
        net_puts("[VirtIO-Net] TX queue setup failed!\n");
        kfree(drv);
        virtio_destroy(vdev);
        return NULL;
    }
    
    /* Lire l'adresse MAC depuis la config space */
    for (int i = 0; i < 6; i++) {
        drv->mac_addr[i] = vdev->ops->read_config8(vdev, i);
    }
    
    net_puts("[VirtIO-Net] MAC Address: ");
    for (int i = 0; i < 6; i++) {
        net_put_hex_byte(drv->mac_addr[i]);
        if (i < 5) net_putc(':');
    }
    net_puts("\n");
    
    /* Configurer l'IRQ */
    uint8_t irq = pci_dev->interrupt_line;
    net_puts("[VirtIO-Net] IRQ: ");
    net_put_dec(irq);
    net_puts("\n");
    
    if (irq != 11) {
        extern void irq11_handler(void);
        idt_set_gate(32 + irq, (uint32_t)(uintptr_t)irq11_handler, 0x08, 0x8E);
    }
    
    /* Remplir la queue RX */
    virtio_net_refill_rx(drv);
    
    /* Finaliser l'initialisation */
    if (virtio_finalize_init(vdev) < 0) {
        net_puts("[VirtIO-Net] Failed to finalize init!\n");
        kfree(drv);
        virtio_destroy(vdev);
        return NULL;
    }
    
    drv->initialized = true;
    g_driver = drv;
    
    /* Créer et enregistrer la NetInterface */
    g_netif = (NetInterface *)kmalloc(sizeof(NetInterface));
    if (g_netif != NULL) {
        g_netif->name[0] = 'e';
        g_netif->name[1] = 't';
        g_netif->name[2] = 'h';
        g_netif->name[3] = '0';
        g_netif->name[4] = '\0';
        
        for (int i = 0; i < 6; i++) {
            g_netif->mac_addr[i] = drv->mac_addr[i];
        }
        
        g_netif->ip_addr = 0;
        g_netif->netmask = 0;
        g_netif->gateway = 0;
        g_netif->dns_server = 0;
        g_netif->flags = NETIF_FLAG_UP | NETIF_FLAG_RUNNING;
        g_netif->send = virtio_netif_send;
        g_netif->driver_data = drv;
        g_netif->packets_rx = 0;
        g_netif->packets_tx = 0;
        g_netif->bytes_rx = 0;
        g_netif->bytes_tx = 0;
        g_netif->errors = 0;
        
        netdev_register(g_netif);
    }
    
    net_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    net_puts("[VirtIO-Net] Driver initialized successfully!\n");
    net_reset_color();
    
    /* Retourner un pointeur compatible avec l'ancienne API */
    return (VirtIONetDevice *)drv;
}

bool virtio_net_send(VirtIONetDevice *dev, const uint8_t *data, uint16_t len) {
    VirtioNetDriver *drv = (VirtioNetDriver *)dev;
    if (drv == NULL || g_netif == NULL) {
        return false;
    }
    return virtio_netif_send(g_netif, (uint8_t *)data, len) > 0;
}

VirtIONetDevice *virtio_net_get_device(void) {
    return (VirtIONetDevice *)g_driver;
}

void virtio_net_poll(void) {
    if (g_driver != NULL && g_driver->initialized) {
        /* Vérifier les interruptions en polling */
        virtio_net_irq_handler_internal();
    }
}

bool virtio_net_is_mmio(VirtIONetDevice *dev) {
    VirtioNetDriver *drv = (VirtioNetDriver *)dev;
    if (drv == NULL || drv->vdev == NULL) {
        return false;
    }
    return drv->vdev->transport.pci.use_mmio;
}

void virtio_net_force_access_mode(virtio_access_mode_t mode) {
    /* Non implémenté - le mode est auto-détecté */
    (void)mode;
}
