/* src/drivers/gpu/gpu.h - Couche d'abstraction GPU a pilotes multiples.
 *
 * Un pilote (virtio-gpu aujourd'hui ; Bochs/VMSVGA/Intel/AMD plus tard)
 * decrit ses operations dans un gpu_driver_t et l'enregistre aupres du
 * registre. gpu_init() parcourt les peripheriques PCI, choisit le pilote
 * compatible de plus haute priorite et le rend "actif". L'API userland
 * (SYS_GPU_*) ne depend que de cette couche. Sans pilote actif, l'affichage
 * reste le framebuffer Limine (repli inchange).
 *
 * Contrat d'un pilote : voir docs/DRIVERS.md (section "GPU") et le squelette
 * src/drivers/gpu/gpu_driver_template.c.example. UP uniquement, pas de verrou. */
#ifndef GPU_H
#define GPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../pci.h"
#include "../../include/gpu_abi.h"

struct gpu_device;

/* Surface d'affichage scanout geree par le noyau (backing contigu). */
typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t pitch;   /* octets par ligne */
  uint32_t format;  /* ALOS_GPU_FORMAT_* */
  uint32_t *pixels; /* adresse virtuelle noyau */
  uint64_t phys;    /* adresse physique contigue */
  size_t size;      /* octets */
} gpu_surface_t;

typedef struct gpu_driver {
  const char *name;
  int priority; /* plus grand = prefere quand plusieurs pilotes correspondent */
  /* Obligatoire : ce pilote prend-il ce peripherique PCI ? (sans effet de bord) */
  bool (*probe)(const PCIDevice *pci);
  /* Obligatoire : initialise le materiel, fournit une surface scanout dans
   * dev->surface et dev->caps. Retourne 0 ou un errno negatif. */
  int (*init)(struct gpu_device *dev, PCIDevice *pci);
  /* Obligatoire si le pilote a besoin d'un flush explicite
   * (ALOS_GPU_CAP_EXPLICIT_FLUSH) : publie [x,y,w,h] de la surface. */
  int (*flush)(struct gpu_device *dev, uint32_t x, uint32_t y, uint32_t w,
               uint32_t h);
  /* Optionnel (NULL = non supporte) : changement de mode -> ALOS_GPU_CAP_MODESET */
  int (*set_mode)(struct gpu_device *dev, uint32_t width, uint32_t height);
  /* Optionnel : liberation (reset du device). */
  void (*shutdown)(struct gpu_device *dev);
} gpu_driver_t;

typedef struct gpu_device {
  const gpu_driver_t *driver;
  PCIDevice *pci;
  void *priv; /* etat prive du pilote */
  gpu_surface_t surface;
  uint32_t caps; /* ALOS_GPU_CAP_* */
} gpu_device_t;

/* Registre : enregistrement a l'init du noyau (avant gpu_init). */
int gpu_register_driver(const gpu_driver_t *driver);

/* 0 si un GPU est actif ; -ENOSYS si aucun peripherique compatible
 * (repli Limine) ; autre errno negatif si les pilotes compatibles ont tous
 * echoue. Idempotent. */
int gpu_init(void);

gpu_device_t *gpu_get_active(void); /* NULL si repli Limine */
int gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int gpu_set_mode(uint32_t width, uint32_t height);
int gpu_query(alos_gpu_caps_t *out);

/* Pilotes integres (declares ici pour le registre statique). */
extern const gpu_driver_t virtio_gpu_driver;

/* Fond bleu 0x000040C0 + rectangle rouge 0x00FF0000 en [64,64,256,128], flush,
 * journal "gpu selftest: rectangle flushed". */
int gpu_selftest(void);

#endif
