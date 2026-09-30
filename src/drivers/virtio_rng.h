#ifndef ALOS_VIRTIO_RNG_H
#define ALOS_VIRTIO_RNG_H

#include <stddef.h>
#include <stdint.h>

/* Apres PCI/PMM et les gates reseau; installer les deux hooks IRQ avant. */
int virtio_rng_init(void);
/* Hook avant EOI, y compris si cette ligne est partagee avec le reseau. */
void virtio_rng_handle_irq(uint8_t irq);
/* Stub ASM partage ne fournissant pas le vecteur reel; aucun EOI ici. */
void virtio_rng_handle_shared_irq(void);
int virtio_rng_read(void *buffer, size_t length);

#endif
