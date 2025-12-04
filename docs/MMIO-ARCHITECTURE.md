# Architecture MMIO d'ALOS

## Introduction

Ce document décrit l'architecture MMIO (Memory-Mapped I/O) implémentée dans ALOS pour remplacer les accès PIO (Port I/O) traditionnels. Cette migration améliore les performances et modernise le code pour une meilleure compatibilité avec les architectures modernes.

## Pourquoi MMIO ?

### Avantages du MMIO sur x86

1. **Utilisation de tous les registres généraux** : Contrairement aux instructions PIO (`IN`/`OUT`) qui sont limitées aux registres `EAX`/`AX`/`AL`, les accès MMIO peuvent utiliser n'importe quel registre général.

2. **Meilleures performances** : Les accès mémoire bénéficient du pipelining CPU et peuvent être optimisés par le compilateur (dans les limites du `volatile`).

3. **Compatibilité PCIe** : Les périphériques PCIe modernes utilisent principalement MMIO.

4. **Simplicité du code** : Les accès MMIO ressemblent à des accès mémoire normaux, rendant le code plus lisible.

### Inconvénients à considérer

1. **Nécessite un mapping mémoire** : Les régions MMIO doivent être mappées dans l'espace d'adressage virtuel.

2. **Attributs de page spéciaux** : Les pages MMIO doivent être marquées non-cachables.

3. **Consommation d'espace d'adressage** : Chaque région MMIO consomme de l'espace d'adressage virtuel.

## Architecture du sous-système MMIO

### Structure des fichiers

```
kernel/
└── mmio/
    ├── mmio.h          # API d'abstraction MMIO
    ├── mmio.c          # Implémentation read/write et ioremap
    └── pci_mmio.h/c    # Configuration PCI/PCIe et parsing des BARs
```

### Composants principaux

#### 1. Fonctions de lecture/écriture MMIO

```c
/* Lecture */
uint8_t  mmio_read8(mmio_addr_t addr);
uint16_t mmio_read16(mmio_addr_t addr);
uint32_t mmio_read32(mmio_addr_t addr);
uint64_t mmio_read64(mmio_addr_t addr);

/* Écriture */
void mmio_write8(mmio_addr_t addr, uint8_t value);
void mmio_write16(mmio_addr_t addr, uint16_t value);
void mmio_write32(mmio_addr_t addr, uint32_t value);
void mmio_write64(mmio_addr_t addr, uint64_t value);
```

Ces fonctions garantissent :
- La largeur exacte de l'accès mémoire
- Pas d'optimisation par le compilateur (utilisation de `volatile`)
- L'ordre des accès préservé

#### 2. Barriers de synchronisation

```c
void mmio_rmb(void);   /* Barrier de lecture */
void mmio_wmb(void);   /* Barrier d'écriture */
void mmio_mb(void);    /* Barrier complète */
void mmiowb(void);     /* Barrier pour release de spinlock */
```

Sur x86 avec le modèle TSO (Total Store Order), les barriers sont généralement légères, mais elles sont essentielles pour la portabilité et la correction.

#### 3. Mapping MMIO (ioremap)

```c
/* Mapper une région physique MMIO */
mmio_addr_t ioremap(uint32_t phys_addr, uint32_t size);

/* Libérer un mapping */
void iounmap(mmio_addr_t virt_addr, uint32_t size);

/* Version avec flags personnalisés */
mmio_addr_t ioremap_flags(uint32_t phys_addr, uint32_t size, uint32_t flags);
```

La fonction `ioremap()` :
1. Aligne les adresses sur `PAGE_SIZE` (4 KiB)
2. Alloue une adresse virtuelle dans la zone MMIO (16-256 Mo)
3. Mappe les pages avec l'attribut `PAGE_NOCACHE`
4. Enregistre la région pour éviter les conflits

#### 4. Parsing des BARs PCI

```c
/* Parser tous les BARs d'un device */
int pci_parse_bars(PCIDevice* pci_dev, pci_device_bars_t* bars);

/* Trouver un BAR MMIO */
pci_bar_info_t* pci_find_mmio_bar(pci_device_bars_t* bars);

/* Mapper un BAR */
void* pci_map_bar(pci_bar_info_t* bar_info);
```

Le parsing des BARs détecte automatiquement :
- Le type de région (MMIO vs PIO) via le bit 0
- La largeur d'adresse (32-bit vs 64-bit)
- La taille de la région (via l'écriture de 0xFFFFFFFF)
- L'attribut prefetchable

## Utilisation dans les drivers

### Exemple : Driver PCNet avec support dual PIO/MMIO

Le driver PCNet a été migré pour supporter les deux modes d'accès :

```c
/* Structure du device avec support MMIO */
typedef struct {
  PCIDevice *pci_dev;
  pcnet_access_mode_t access_mode;  /* PIO ou MMIO */
  uint32_t io_base;                 /* Pour PIO */
  volatile void *mmio_base;         /* Pour MMIO */
  /* ... */
} PCNetDevice;

/* Fonction de lecture CSR avec dispatch automatique */
uint32_t pcnet_read_csr(PCNetDevice *dev, uint32_t csr_no) {
  if (dev->access_mode == PCNET_ACCESS_MMIO && dev->mmio_base != NULL) {
    return pcnet_read_csr_mmio(dev, csr_no);
  } else {
    return pcnet_read_csr_pio(dev, csr_no);
  }
}

/* Implémentation MMIO */
static uint32_t pcnet_read_csr_mmio(PCNetDevice *dev, uint32_t csr_no) {
  asm volatile("cli");
  mmio_write32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RAP), csr_no);
  mmio_wmb();
  uint32_t value = mmio_read32(MMIO_REG(dev->mmio_base, PCNET_MMIO_RDP));
  asm volatile("sti");
  return value;
}
```

### Détection automatique du mode

Lors de l'initialisation, le driver :
1. Parse les BARs PCI
2. Détecte si un BAR MMIO est disponible
3. Mappe la région MMIO si disponible
4. Sélectionne automatiquement le meilleur mode

```c
/* Dans pcnet_init() */
pci_device_bars_t bars;
if (pci_parse_bars(pci_dev, &bars) == 0) {
  pci_bar_info_t *mmio_bar = pci_find_mmio_bar(&bars);
  
  if (mmio_bar != NULL && mmio_bar->size >= 32) {
    dev->mmio_base = pci_map_bar(mmio_bar);
    if (dev->mmio_base != NULL) {
      dev->access_mode = PCNET_ACCESS_MMIO;
    }
  }
}
```

## Guide de migration pour les drivers

### Étape 1 : Ajouter les includes nécessaires

```c
#include "../../kernel/mmio/mmio.h"
#include "../../kernel/mmio/pci_mmio.h"
```

### Étape 2 : Modifier la structure du device

Ajouter les champs pour le support MMIO :

```c
typedef struct {
  /* Existant */
  uint32_t io_base;
  
  /* Nouveau pour MMIO */
  volatile void *mmio_base;
  uint32_t mmio_phys;
  uint32_t mmio_size;
  my_access_mode_t access_mode;
} MyDevice;
```

### Étape 3 : Créer les fonctions d'accès MMIO

Pour chaque fonction PIO existante, créer une version MMIO :

```c
/* PIO existant */
static uint32_t my_read_reg_pio(MyDevice *dev, uint16_t reg) {
  return inl(dev->io_base + reg);
}

/* Nouveau MMIO */
static uint32_t my_read_reg_mmio(MyDevice *dev, uint16_t reg) {
  return mmio_read32(MMIO_REG(dev->mmio_base, reg));
}

/* Dispatch */
uint32_t my_read_reg(MyDevice *dev, uint16_t reg) {
  if (dev->access_mode == ACCESS_MMIO) {
    return my_read_reg_mmio(dev, reg);
  }
  return my_read_reg_pio(dev, reg);
}
```

### Étape 4 : Détecter et configurer MMIO dans init()

```c
MyDevice* my_init(PCIDevice *pci_dev) {
  MyDevice *dev = kmalloc(sizeof(MyDevice));
  
  /* Initialiser avec PIO par défaut */
  dev->io_base = pci_dev->bar0 & 0xFFFFFFFC;
  dev->access_mode = ACCESS_PIO;
  
  /* Tenter d'activer MMIO */
  pci_device_bars_t bars;
  if (pci_parse_bars(pci_dev, &bars) == 0) {
    pci_bar_info_t *mmio_bar = pci_find_mmio_bar(&bars);
    if (mmio_bar != NULL) {
      dev->mmio_base = pci_map_bar(mmio_bar);
      if (dev->mmio_base != NULL) {
        dev->access_mode = ACCESS_MMIO;
      }
    }
  }
  
  return dev;
}
```

### Étape 5 : Utiliser les barriers appropriées

```c
/* Après une écriture critique */
mmio_write32(MMIO_REG(dev->mmio_base, REG_COMMAND), cmd);
mmiowb();  /* Garantir que l'écriture est visible */

/* Avant de lire après une écriture */
mmio_write32(MMIO_REG(dev->mmio_base, REG_INDEX), idx);
mmio_wmb();  /* Garantir l'ordre */
value = mmio_read32(MMIO_REG(dev->mmio_base, REG_DATA));
```

## Considérations de performance

### Mesure de performance

Pour comparer PIO vs MMIO, vous pouvez utiliser le compteur de ticks :

```c
uint64_t start = timer_get_ticks();
for (int i = 0; i < 10000; i++) {
  my_read_reg(dev, REG_STATUS);
}
uint64_t end = timer_get_ticks();
console_puts("Temps: ");
console_put_dec(end - start);
console_puts(" ticks\n");
```

### Optimisations possibles

1. **Regrouper les accès** : Plusieurs lectures/écritures consécutives peuvent être plus efficaces en MMIO.

2. **Éviter les barriers inutiles** : Sur x86, les barriers sont légères mais pas gratuites.

3. **Utiliser le prefetching** : Pour les régions prefetchable, le CPU peut optimiser les accès.

## Débogage

### Afficher les régions MMIO

```c
mmio_dump_regions();
```

### Afficher les BARs d'un device

```c
pci_device_bars_t bars;
pci_parse_bars(pci_dev, &bars);
pci_dump_bars(&bars);
```

### Vérifier le mode d'accès

```c
if (pcnet_is_mmio(dev)) {
  console_puts("Mode: MMIO\n");
} else {
  console_puts("Mode: PIO\n");
}
```

## Références

- [Memory-mapped I/O - Wikipedia](https://en.wikipedia.org/wiki/Memory-mapped_I/O_and_port-mapped_I/O)
- [Linux Kernel - Bus/Virtual/Physical Mapping](https://www.kernel.org/doc/html/v5.18/core-api/bus-virt-phys-mapping.html)
- [PCI Local Bus Specification](https://pcisig.com/)

## Historique des modifications

- **v1.0** : Implémentation initiale avec support PCNet
  - Abstraction MMIO (mmio.h/mmio.c)
  - Parsing des BARs PCI (pci_mmio.h/pci_mmio.c)
  - Migration du driver PCNet avec support dual PIO/MMIO
