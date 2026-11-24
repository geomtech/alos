/* VirtIO GPU 2D (PCI modern, VirtIO 1.x) : controlq polle, UP uniquement.
 *
 * Perimetre : un scanout (0), une ressource 2D B8G8R8X8 adossee a une memoire
 * physique contigue, transfer + flush. Pas de 3D (virgl/venus), pas de curseur
 * materiel, pas d'IRQ ni d'EDID. Le device virtio-vga (classe VGA) n'est PAS
 * pris : le framebuffer Limine reste proprietaire de cet affichage. */
#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t pitch;      /* octets par ligne (width * 4) */
  uint32_t *pixels;    /* backing contigu, adresse virtuelle noyau (B8G8R8X8) */
  uint64_t phys;
  uint32_t scanout;
  uint32_t resource_id;
} virtio_gpu_info_t;

/* 0 si initialise ; -ENOSYS si absent ; autre errno negatif en cas d'echec. */
int virtio_gpu_init(void);
bool virtio_gpu_available(void);
const virtio_gpu_info_t *virtio_gpu_get_info(void);

/* Envoie [x,y,w,h] du backing vers l'hote puis rafraichit le scanout. */
int virtio_gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Dessine un motif de test connu (fond bleu 0x000040C0, rectangle rouge
 * 0x00FF0000 en [64,64,256,128]) et le flushe ; journalise le resultat. */
int virtio_gpu_selftest(void);

#endif
