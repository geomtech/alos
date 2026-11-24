/* src/drivers/gpu/gpu.c - Registre de pilotes GPU et peripherique actif. */
#include "gpu.h"
#include "../../kernel/klog.h"
#include "../../include/errno.h"
#include "../../include/string.h"

#define GPU_MAX_DRIVERS 8

static const gpu_driver_t *drivers[GPU_MAX_DRIVERS];
static unsigned driver_count;
static gpu_device_t active;
static int init_state; /* 0 non tente, 1 actif, negatif: resultat de l'echec */

int gpu_register_driver(const gpu_driver_t *driver) {
  if (!driver || !driver->name || !driver->probe || !driver->init)
    return -EINVAL;
  for (unsigned i = 0; i < driver_count; i++)
    if (drivers[i] == driver) return 0;
  if (driver_count >= GPU_MAX_DRIVERS) return -ENOMEM;
  drivers[driver_count++] = driver;
  return 0;
}

static void register_builtin(void) {
  static bool done;
  if (done) return;
  done = true;
  (void)gpu_register_driver(&virtio_gpu_driver);
  /* Futurs pilotes : (void)gpu_register_driver(&bochs_gpu_driver); etc. */
}

/* Pilote compatible de plus haute priorite pour ce device, hors deja essayes. */
static const gpu_driver_t *best_driver(const PCIDevice *pci, unsigned tried) {
  const gpu_driver_t *best = NULL;
  for (unsigned i = 0; i < driver_count; i++) {
    if (tried & (1u << i)) continue;
    if (!drivers[i]->probe(pci)) continue;
    if (!best || drivers[i]->priority > best->priority) best = drivers[i];
  }
  return best;
}

static unsigned driver_index(const gpu_driver_t *driver) {
  for (unsigned i = 0; i < driver_count; i++)
    if (drivers[i] == driver) return i;
  return 0;
}

int gpu_init(void) {
  if (init_state) return init_state == 1 ? 0 : init_state;
  register_builtin();
  int result = -ENOSYS;
  for (PCIDevice *pci = pci_get_devices(); pci; pci = pci->next) {
    unsigned tried = 0;
    const gpu_driver_t *driver;
    while ((driver = best_driver(pci, tried)) != NULL) {
      tried |= 1u << driver_index(driver);
      gpu_device_t candidate;
      memset(&candidate, 0, sizeof(candidate));
      candidate.driver = driver;
      candidate.pci = pci;
      int error = driver->init(&candidate, pci);
      if (!error) {
        active = candidate;
        init_state = 1;
        KLOG_INFO("GPU", driver->name);
        return 0;
      }
      KLOG_ERROR_DEC("GPU", "driver init failed, errno: ", (uint32_t)-error);
      if (result == -ENOSYS || result == -ENOTSUP) result = error;
    }
  }
  init_state = result;
  return result;
}

gpu_device_t *gpu_get_active(void) { return init_state == 1 ? &active : NULL; }

int gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  gpu_device_t *dev = gpu_get_active();
  if (!dev) return -ENODEV;
  if (!w || !h || x >= dev->surface.width || y >= dev->surface.height ||
      w > dev->surface.width - x || h > dev->surface.height - y)
    return -EINVAL;
  if (!dev->driver->flush) return 0; /* scanout direct, rien a publier */
  return dev->driver->flush(dev, x, y, w, h);
}

int gpu_set_mode(uint32_t width, uint32_t height) {
  gpu_device_t *dev = gpu_get_active();
  if (!dev) return -ENODEV;
  if (!dev->driver->set_mode) return -ENOTSUP;
  return dev->driver->set_mode(dev, width, height);
}

int gpu_query(alos_gpu_caps_t *out) {
  gpu_device_t *dev = gpu_get_active();
  if (!dev) return -ENODEV;
  memset(out, 0, sizeof(*out));
  size_t n = strlen(dev->driver->name);
  if (n >= sizeof(out->driver)) n = sizeof(out->driver) - 1;
  memcpy(out->driver, dev->driver->name, n);
  out->caps = dev->caps;
  out->vendor = dev->pci ? dev->pci->vendor_id : 0;
  out->device = dev->pci ? dev->pci->device_id : 0;
  out->width = dev->surface.width;
  out->height = dev->surface.height;
  out->pitch = dev->surface.pitch;
  out->format = dev->surface.format;
  return 0;
}

int gpu_selftest(void) {
  gpu_device_t *dev = gpu_get_active();
  if (!dev) return -ENODEV;
  gpu_surface_t *s = &dev->surface;
  for (uint32_t y = 0; y < s->height; y++)
    for (uint32_t x = 0; x < s->width; x++)
      s->pixels[(size_t)y * (s->pitch / 4) + x] = 0x000040C0;
  for (uint32_t y = 64; y < 64 + 128 && y < s->height; y++)
    for (uint32_t x = 64; x < 64 + 256 && x < s->width; x++)
      s->pixels[(size_t)y * (s->pitch / 4) + x] = 0x00FF0000;
  int error = gpu_flush(0, 0, s->width, s->height);
  if (error) {
    KLOG_ERROR_DEC("GPU", "gpu selftest flush failed errno: ", (uint32_t)-error);
    return error;
  }
  KLOG_INFO("GPU", "gpu selftest: rectangle flushed");
  return 0;
}
