/* virtio-gpu 2D pour le display owner (voir docs/chromium-port.md). */
#ifndef _SYS_GPU_H
#define _SYS_GPU_H

#include "../../../../include/gpu_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mappe la surface (ecriture directe) ; -1/errno : EPERM (pas display owner),
 * ENODEV (aucun virtio-gpu 2D), ENOMEM. */
int gpu_map(alos_gpu_info_t *info);
/* Capacites du pilote actif (nom, vendor/device PCI, caps ALOS_GPU_CAP_*, mode).
 * ENODEV : aucun GPU, le framebuffer Limine est utilise. Tout processus. */
int gpu_query(alos_gpu_caps_t *caps);
/* Envoie le rectangle a l'hote et rafraichit le scanout. */
int gpu_flush(unsigned x, unsigned y, unsigned width, unsigned height);

#ifdef __cplusplus
}
#endif

#endif
