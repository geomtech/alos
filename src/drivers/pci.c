/* src/drivers/pci.c - PCI Bus Driver Implementation */
#include "pci.h"
#include "../arch/x86/io.h"
#include "../kernel/klog.h"
#include "../mm/kheap.h"

/* Liste chaînée globale des périphériques PCI */
static PCIDevice *pci_devices = NULL;
static int pci_device_count = 0;

/* ============================================ */
/*           Helper Functions                   */
/* ============================================ */

void pci_enable_bus_mastering(PCIDevice *dev) {
  /* Lire le Command Register PCI (offset 0x04) */
  uint32_t command =
      pci_config_read_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND);

  /*
   * Bits du Command Register:
   * Bit 0: I/O Space Enable
   * Bit 1: Memory Space Enable
   * Bit 2: Bus Master Enable <- C'est celui qu'on veut !
   */
  command |= (1 << 0); /* Enable I/O Space */
  command |= (1 << 1); /* Enable Memory Space */
  command |= (1 << 2); /* Enable Bus Mastering */

  /* Écrire le nouveau Command Register */
  pci_config_write_dword(dev->bus, dev->slot, dev->func, PCI_COMMAND, command);

  /* Relire pour vérifier (optionnel, pour debug) */
  /* uint32_t verify = pci_config_read_dword(dev->bus, dev->slot, dev->func,
   * PCI_COMMAND); */
}

/**
 * Construit l'adresse PCI pour accéder au Configuration Space.
 *
 * Format de l'adresse (32 bits):
 * Bit 31    : Enable Bit (doit être 1)
 * Bits 30-24: Réservés
 * Bits 23-16: Bus Number (0-255)
 * Bits 15-11: Device/Slot Number (0-31)
 * Bits 10-8 : Function Number (0-7)
 * Bits 7-0  : Register Offset (aligné sur 4 octets, bits 1-0 = 0)
 */
static inline uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func,
                                        uint8_t offset) {
  return (
      uint32_t)(((uint32_t)1 << 31) |             /* Enable Bit */
                ((uint32_t)bus << 16) |           /* Bus Number */
                ((uint32_t)(slot & 0x1F) << 11) | /* Device Number (5 bits) */
                ((uint32_t)(func & 0x07) << 8) |  /* Function Number (3 bits) */
                ((uint32_t)(offset & 0xFC)) /* Register Offset (aligné sur 4) */
  );
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func,
                              uint8_t offset) {
  uint32_t address = pci_make_address(bus, slot, func, offset);

  /* Écrire l'adresse sur le port CONFIG_ADDRESS */
  outl(PCI_CONFIG_ADDRESS, address);

  /* Lire la donnée sur le port CONFIG_DATA */
  /* On lit 32 bits et on extrait le mot de 16 bits approprié */
  uint32_t data = inl(PCI_CONFIG_DATA);

  /* Si offset est pair mais pas multiple de 4, on prend les 16 bits hauts */
  return (uint16_t)((data >> ((offset & 2) * 8)) & 0xFFFF);
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func,
                               uint8_t offset) {
  uint32_t address = pci_make_address(bus, slot, func, offset);

  outl(PCI_CONFIG_ADDRESS, address);
  return inl(PCI_CONFIG_DATA);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func,
                             uint8_t offset) {
  uint32_t address = pci_make_address(bus, slot, func, offset);

  outl(PCI_CONFIG_ADDRESS, address);
  uint32_t data = inl(PCI_CONFIG_DATA);

  /* Extraire l'octet approprié */
  return (uint8_t)((data >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t offset, uint32_t value) {
  uint32_t address = pci_make_address(bus, slot, func, offset);

  outl(PCI_CONFIG_ADDRESS, address);
  outl(PCI_CONFIG_DATA, value);
}

/**
 * Ajoute un périphérique à la liste chaînée.
 */
static void pci_add_device(PCIDevice *device) {
  device->next = NULL;

  if (pci_devices == NULL) {
    pci_devices = device;
  } else {
    /* Ajouter à la fin de la liste */
    PCIDevice *current = pci_devices;
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = device;
  }

  pci_device_count++;
}

/**
 * Vérifie si un périphérique existe à cette adresse et l'ajoute si oui.
 */
static void pci_check_device(uint8_t bus, uint8_t slot, uint8_t func) {
  uint16_t vendor_id = pci_config_read_word(bus, slot, func, PCI_VENDOR_ID);

  /* 0xFFFF signifie qu'il n'y a pas de périphérique */
  if (vendor_id == 0xFFFF) {
    return;
  }

  /* Périphérique trouvé ! Allouer une structure */
  PCIDevice *device = (PCIDevice *)kmalloc(sizeof(PCIDevice));
  if (device == NULL) {
    return; /* Pas de mémoire disponible */
  }

  /* Remplir les informations de base */
  device->vendor_id = vendor_id;
  device->device_id = pci_config_read_word(bus, slot, func, PCI_DEVICE_ID);
  device->bus = bus;
  device->slot = slot;
  device->func = func;

  /* Classe et sous-classe */
  device->class_code =
      (uint8_t)(pci_config_read_word(bus, slot, func, PCI_CLASS) >> 8);
  device->subclass =
      (uint8_t)(pci_config_read_word(bus, slot, func, PCI_SUBCLASS) & 0xFF);
  device->prog_if =
      (uint8_t)(pci_config_read_word(bus, slot, func, PCI_PROG_IF) & 0xFF);
  device->revision =
      (uint8_t)(pci_config_read_word(bus, slot, func, PCI_REVISION_ID) & 0xFF);

  /* Interrupt */
  device->interrupt_line =
      (uint8_t)(pci_config_read_word(bus, slot, func, PCI_INTERRUPT_LINE) &
                0xFF);

  /* Base Address Registers (BARs) */
  device->bar0 = pci_config_read_dword(bus, slot, func, PCI_BAR0);
  device->bar1 = pci_config_read_dword(bus, slot, func, PCI_BAR1);
  device->bar2 = pci_config_read_dword(bus, slot, func, PCI_BAR2);
  device->bar3 = pci_config_read_dword(bus, slot, func, PCI_BAR3);
  device->bar4 = pci_config_read_dword(bus, slot, func, PCI_BAR4);
  device->bar5 = pci_config_read_dword(bus, slot, func, PCI_BAR5);

  /* Ajouter à la liste */
  pci_add_device(device);

  /* Afficher l'info */
  klog(LOG_INFO, "PCI", "Found device ");
  klog(LOG_INFO, "PCI", pci_get_vendor_name(device->vendor_id));
  klog_hex(LOG_INFO, "PCI", "  Vendor:Device = ", device->vendor_id);
  klog_hex(LOG_DEBUG, "PCI", "  BAR0 = ", device->bar0);
}

void pci_probe(void) {
  KLOG_INFO("PCI", "Starting PCI bus enumeration...");

  /* Scanner tous les bus, slots et fonctions */
  for (int bus = 0; bus < 256; bus++) {
    for (int slot = 0; slot < 32; slot++) {
      /* Vérifier la fonction 0 */
      uint16_t vendor = pci_config_read_word(bus, slot, 0, PCI_VENDOR_ID);
      if (vendor == 0xFFFF) {
        continue; /* Pas de périphérique sur ce slot */
      }

      /* Périphérique présent, vérifier si multi-fonction */
      pci_check_device(bus, slot, 0);

      /* Vérifier le Header Type pour les périphériques multi-fonction */
      uint8_t header_type =
          (uint8_t)(pci_config_read_word(bus, slot, 0, PCI_HEADER_TYPE) & 0xFF);

      if (header_type & 0x80) {
        /* Périphérique multi-fonction, scanner les fonctions 1-7 */
        for (int func = 1; func < 8; func++) {
          pci_check_device(bus, slot, func);
        }
      }
    }
  }

  KLOG_INFO_DEC("PCI", "PCI scan complete, devices found: ", pci_device_count);
}

PCIDevice *pci_get_device(uint16_t vendor_id, uint16_t device_id) {
  PCIDevice *current = pci_devices;

  while (current != NULL) {
    if (current->vendor_id == vendor_id && current->device_id == device_id) {
      return current;
    }
    current = current->next;
  }

  return NULL;
}

PCIDevice *pci_get_device_by_class(uint8_t class_code, uint8_t subclass) {
  PCIDevice *current = pci_devices;

  while (current != NULL) {
    if (current->class_code == class_code && current->subclass == subclass) {
      return current;
    }
    current = current->next;
  }

  return NULL;
}

PCIDevice *pci_get_devices(void) { return pci_devices; }

int pci_get_device_count(void) { return pci_device_count; }

const char *pci_get_vendor_name(uint16_t vendor_id) {
  switch (vendor_id) {
  case PCI_VENDOR_AMD:
    return "AMD";
  case PCI_VENDOR_INTEL:
    return "Intel";
  case PCI_VENDOR_NVIDIA:
    return "NVIDIA";
  case PCI_VENDOR_REALTEK:
    return "Realtek";
  case PCI_VENDOR_QEMU:
    return "QEMU";
  case 0x1013:
    return "Cirrus Logic";
  case 0x1033:
    return "NEC";
  case 0x1106:
    return "VIA";
  case 0x1274:
    return "Ensoniq";
  case 0x15AD:
    return "VMware";
  case 0x1AF4:
    return "Red Hat (VirtIO)";
  case 0x80EE:
    return "VirtualBox";
  default:
    return "Unknown";
  }
}

const char *pci_get_class_name(uint8_t class_code) {
  switch (class_code) {
  case 0x00:
    return "Unclassified";
  case 0x01:
    return "Storage";
  case 0x02:
    return "Network";
  case 0x03:
    return "Display";
  case 0x04:
    return "Multimedia";
  case 0x05:
    return "Memory";
  case 0x06:
    return "Bridge";
  case 0x07:
    return "Communication";
  case 0x08:
    return "System";
  case 0x09:
    return "Input";
  case 0x0A:
    return "Docking";
  case 0x0B:
    return "Processor";
  case 0x0C:
    return "Serial Bus";
  case 0x0D:
    return "Wireless";
  case 0x0E:
    return "Intelligent";
  case 0x0F:
    return "Satellite";
  case 0x10:
    return "Encryption";
  case 0x11:
    return "Signal Proc";
  default:
    return "Other";
  }
}
