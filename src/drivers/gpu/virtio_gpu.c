/* VirtIO GPU 2D, PCI modern. Voir virtio_gpu.h pour le perimetre et les
 * limites. Ecrit directement sur les capabilities modern : le transport
 * generique (virtio_transport.c) stocke des adresses physiques 32 bits et ne
 * negocie pas VIRTIO_F_VERSION_1, ce qui ne convient pas a virtio-gpu. */
#include "virtio_gpu.h"
#include "pci.h"
#include "virtio/virtio_pci_modern.h"
#include "virtio/virtio_transport.h"
#include "../mm/pmm.h"
#include "../kernel/timer.h"
#include "../kernel/klog.h"
#include "../include/errno.h"
#include "../include/string.h"

#define VIRTIO_GPU_PCI_ID 0x1050 /* 0x1040 + device id 16 */
#define VIRTIO_F_VERSION_1_HI (1u << 0) /* bit 32, page de features 1 */
#define GPU_QUEUE_SIZE 64

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2
#define VIRTIO_GPU_MAX_SCANOUTS 16

typedef struct {
  uint32_t type, flags;
  uint64_t fence_id;
  uint32_t ctx_id;
  uint32_t ring_idx_padding;
} __attribute__((packed)) gpu_hdr_t;

typedef struct { uint32_t x, y, width, height; } __attribute__((packed)) gpu_rect_t;

typedef struct {
  gpu_hdr_t hdr;
  struct {
    gpu_rect_t r;
    uint32_t enabled, flags;
  } pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed)) gpu_display_info_t;

static struct {
  int state; /* 0 non tente, 1 actif, negatif: erreur */
  virtio_pci_modern_t modern;
  volatile virtio_pci_common_cfg_t *common;
  volatile uint16_t *notify;
  uint16_t qsize;
  VirtqDesc *desc;
  volatile VirtqAvail *avail;
  volatile VirtqUsed *used;
  uint16_t avail_idx, last_used;
  uint8_t *cmd; /* page DMA : requete [0,2048), reponse [2048,4096) */
  uint64_t cmd_phys;
  virtio_gpu_info_t info;
  size_t backing_pages;
} gpu;

static void barrier(void) { __asm__ volatile("mfence" ::: "memory"); }

static int gpu_fail(int error, const char *why) {
  KLOG_ERROR("VGPU", why);
  gpu.state = error;
  if (gpu.common) gpu.common->device_status = 0;
  return error;
}

/* Soumet requete [req,req+req_len) + reponse [resp,resp+resp_len) sur la
 * controlq et attend la completion par polling. */
static int gpu_submit(const void *req, uint32_t req_len, void *resp,
                      uint32_t resp_len) {
  if (req_len > 2048 || resp_len > 2048) return -EINVAL;
  uint8_t *rq = gpu.cmd, *rs = gpu.cmd + 2048;
  memcpy(rq, req, req_len);
  memset(rs, 0, resp_len);
  gpu.desc[0].addr = gpu.cmd_phys;
  gpu.desc[0].len = req_len;
  gpu.desc[0].flags = VIRTQ_DESC_F_NEXT;
  gpu.desc[0].next = 1;
  gpu.desc[1].addr = gpu.cmd_phys + 2048;
  gpu.desc[1].len = resp_len;
  gpu.desc[1].flags = VIRTQ_DESC_F_WRITE;
  gpu.desc[1].next = 0;
  gpu.avail->ring[gpu.avail_idx % gpu.qsize] = 0;
  barrier();
  gpu.avail_idx++;
  gpu.avail->idx = gpu.avail_idx;
  barrier();
  *gpu.notify = 0;
  /* Polling borne par iterations : le timer n'avance pas encore au boot. */
  for (uint64_t spin = 0; gpu.used->idx == gpu.last_used; spin++) {
    if (spin > 20000000ull) {
      KLOG_ERROR_HEX("VGPU", "timeout, device status: ", gpu.common->device_status);
      KLOG_ERROR_HEX("VGPU", "timeout, used idx: ", gpu.used->idx);
      return -ETIMEDOUT;
    }
    __asm__ volatile("pause");
  }
  barrier();
  gpu.last_used = gpu.used->idx;
  memcpy(resp, rs, resp_len);
  return 0;
}

static int gpu_command(const void *req, uint32_t req_len, uint32_t expect) {
  gpu_hdr_t resp;
  int error = gpu_submit(req, req_len, &resp, sizeof(resp));
  if (error) return error;
  if (resp.type != expect) {
    KLOG_ERROR_HEX("VGPU", "Unexpected response type: ", resp.type);
    return -EIO;
  }
  return 0;
}

int virtio_gpu_init(void) {
  if (gpu.state) return gpu.state == 1 ? 0 : gpu.state;
  PCIDevice *pci = pci_get_device(0x1af4, VIRTIO_GPU_PCI_ID);
  if (!pci) { gpu.state = -ENOSYS; return -ENOSYS; }
  if (pci->class_code == 0x03 && pci->subclass == 0x00) {
    /* virtio-vga : le framebuffer Limine l'utilise, ne pas le detourner. */
    KLOG_INFO("VGPU", "virtio-vga (VGA class) left to the boot framebuffer");
    gpu.state = -ENOTSUP;
    return -ENOTSUP;
  }
  if (!virtio_pci_modern_detect(pci, &gpu.modern) ||
      virtio_pci_modern_map(pci, &gpu.modern) < 0 ||
      !gpu.modern.common_cfg || !gpu.modern.notify_base)
    return gpu_fail(-ENOTSUP, "virtio-gpu: PCI modern capabilities missing");
  pci_enable_bus_mastering(pci);
  /* Pilote polle : couper INTx (bit 10 de PCI COMMAND). Sans cela, la ligne
   * IRQ partagee avec virtio-net n'est jamais acquittee pour le GPU (tempete
   * d'interruptions dans le handler reseau) ; NO_INTERRUPT sur le ring en plus. */
  uint32_t command = pci_config_read_dword(pci->bus, pci->slot, pci->func, 4);
  pci_config_write_dword(pci->bus, pci->slot, pci->func, 4,
                         (command & 0xffffu) | (1u << 10));
  gpu.common = (volatile virtio_pci_common_cfg_t *)gpu.modern.common_cfg;

  gpu.common->device_status = 0;
  gpu.common->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
  gpu.common->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
  gpu.common->device_feature_select = 1;
  if (!(gpu.common->device_feature & VIRTIO_F_VERSION_1_HI))
    return gpu_fail(-ENOTSUP, "virtio-gpu: VIRTIO_F_VERSION_1 not offered");
  gpu.common->driver_feature_select = 0;
  gpu.common->driver_feature = 0; /* ni VIRGL, ni EDID, ni blob */
  gpu.common->driver_feature_select = 1;
  gpu.common->driver_feature = VIRTIO_F_VERSION_1_HI;
  gpu.common->device_status = VIRTIO_STATUS_ACKNOWLEDGE |
      VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (!(gpu.common->device_status & VIRTIO_STATUS_FEATURES_OK))
    return gpu_fail(-EIO, "virtio-gpu: FEATURES_OK rejected");

  gpu.common->queue_select = 0;
  uint16_t max = gpu.common->queue_size;
  if (!max) return gpu_fail(-ENOTSUP, "virtio-gpu: controlq absent");
  gpu.qsize = max < GPU_QUEUE_SIZE ? max : GPU_QUEUE_SIZE;
  gpu.common->queue_size = gpu.qsize;
  uint8_t *queue = pmm_alloc_blocks(3);
  uint8_t *cmd = pmm_alloc_block();
  if (!queue || !cmd) {
    if (queue) pmm_free_blocks(queue, 3);
    if (cmd) pmm_free_block(cmd);
    return gpu_fail(-ENOMEM, "virtio-gpu: DMA allocation failed");
  }
  memset(queue, 0, 3 * 4096);
  memset(cmd, 0, 4096);
  uint64_t queue_phys = pmm_virt_to_phys(queue);
  gpu.cmd = cmd;
  gpu.cmd_phys = pmm_virt_to_phys(cmd);
  gpu.desc = (VirtqDesc *)queue;
  gpu.avail = (VirtqAvail *)(queue + 16u * gpu.qsize);
  gpu.avail->flags = 1; /* VRING_AVAIL_F_NO_INTERRUPT */
  gpu.used = (VirtqUsed *)(queue + 4096);
  /* Acces 32 bits obligatoires : la common cfg refuse les ecritures 64 bits. */
  volatile uint32_t *q = (volatile uint32_t *)((volatile uint8_t *)gpu.common +
                                              VIRTIO_PCI_COMMON_Q_DESCLO);
  uint64_t desc_phys = queue_phys, avail_phys = queue_phys + 16u * gpu.qsize,
           used_phys = queue_phys + 4096;
  q[0] = (uint32_t)desc_phys;  q[1] = (uint32_t)(desc_phys >> 32);
  q[2] = (uint32_t)avail_phys; q[3] = (uint32_t)(avail_phys >> 32);
  q[4] = (uint32_t)used_phys;  q[5] = (uint32_t)(used_phys >> 32);
  gpu.notify = (volatile uint16_t *)((uint8_t *)gpu.modern.notify_base +
      (uint32_t)gpu.common->queue_notify_off * gpu.modern.notify_off_multiplier);
  gpu.common->queue_enable = 1;
  gpu.common->device_status = VIRTIO_STATUS_ACKNOWLEDGE |
      VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;

  /* Mode du scanout 0 (defaut QEMU : 1280x800). */
  gpu_display_info_t di;
  gpu_hdr_t hdr = {.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO};
  int error = gpu_submit(&hdr, sizeof(hdr), &di, sizeof(di));
  if (error || di.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
    return gpu_fail(error ? error : -EIO, "virtio-gpu: GET_DISPLAY_INFO failed");
  uint32_t w = di.pmodes[0].r.width, h = di.pmodes[0].r.height;
  if (!w || !h) { w = 1024; h = 768; } /* scanout desactive : mode par defaut */
  if (w > 4096 || h > 4096)
    return gpu_fail(-ENOTSUP, "virtio-gpu: scanout mode too large");

  size_t pages = ((size_t)w * h * 4 + 4095) / 4096;
  uint32_t *pixels = pmm_alloc_blocks(pages);
  if (!pixels) return gpu_fail(-ENOMEM, "virtio-gpu: framebuffer allocation failed");
  memset(pixels, 0, pages * 4096);
  gpu.backing_pages = pages;
  gpu.info = (virtio_gpu_info_t){.width = w, .height = h, .pitch = w * 4,
      .pixels = pixels, .phys = pmm_virt_to_phys(pixels), .scanout = 0,
      .resource_id = 1};

  struct { gpu_hdr_t h; uint32_t id, format, width, height; }
      __attribute__((packed)) create = {
      {.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D}, 1,
      VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM, w, h};
  if ((error = gpu_command(&create, sizeof(create), VIRTIO_GPU_RESP_OK_NODATA)))
    return gpu_fail(error, "virtio-gpu: RESOURCE_CREATE_2D failed");
  struct { gpu_hdr_t h; uint32_t id, nr; uint64_t addr; uint32_t len, pad; }
      __attribute__((packed)) attach = {
      {.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING}, 1, 1, gpu.info.phys,
      (uint32_t)(pages * 4096), 0};
  if ((error = gpu_command(&attach, sizeof(attach), VIRTIO_GPU_RESP_OK_NODATA)))
    return gpu_fail(error, "virtio-gpu: ATTACH_BACKING failed");
  struct { gpu_hdr_t h; gpu_rect_t r; uint32_t scanout, id; }
      __attribute__((packed)) scan = {
      {.type = VIRTIO_GPU_CMD_SET_SCANOUT}, {0, 0, w, h}, 0, 1};
  if ((error = gpu_command(&scan, sizeof(scan), VIRTIO_GPU_RESP_OK_NODATA)))
    return gpu_fail(error, "virtio-gpu: SET_SCANOUT failed");

  gpu.state = 1;
  KLOG_INFO_DEC("VGPU", "virtio-gpu 2D scanout 0 width: ", w);
  KLOG_INFO_DEC("VGPU", "virtio-gpu 2D scanout 0 height: ", h);
  return 0;
}

bool virtio_gpu_available(void) { return gpu.state == 1; }

const virtio_gpu_info_t *virtio_gpu_get_info(void) {
  return gpu.state == 1 ? &gpu.info : NULL;
}

int virtio_gpu_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
  if (gpu.state != 1) return -ENODEV;
  if (!w || !h || x >= gpu.info.width || y >= gpu.info.height ||
      w > gpu.info.width - x || h > gpu.info.height - y) return -EINVAL;
  struct { gpu_hdr_t h; gpu_rect_t r; uint64_t offset; uint32_t id, pad; }
      __attribute__((packed)) transfer = {
      {.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D}, {x, y, w, h},
      (uint64_t)y * gpu.info.pitch + (uint64_t)x * 4, 1, 0};
  int error = gpu_command(&transfer, sizeof(transfer), VIRTIO_GPU_RESP_OK_NODATA);
  if (error) return error;
  struct { gpu_hdr_t h; gpu_rect_t r; uint32_t id, pad; }
      __attribute__((packed)) flush = {
      {.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH}, {x, y, w, h}, 1, 0};
  return gpu_command(&flush, sizeof(flush), VIRTIO_GPU_RESP_OK_NODATA);
}

int virtio_gpu_selftest(void) {
  const virtio_gpu_info_t *info = virtio_gpu_get_info();
  if (!info) return -ENODEV;
  for (uint32_t y = 0; y < info->height; y++)
    for (uint32_t x = 0; x < info->width; x++)
      info->pixels[(size_t)y * info->width + x] = 0x000040C0;
  for (uint32_t y = 64; y < 64 + 128 && y < info->height; y++)
    for (uint32_t x = 64; x < 64 + 256 && x < info->width; x++)
      info->pixels[(size_t)y * info->width + x] = 0x00FF0000;
  int error = virtio_gpu_flush(0, 0, info->width, info->height);
  if (error) {
    KLOG_ERROR_DEC("VGPU", "virtio-gpu selftest flush failed errno: ", (uint32_t)-error);
    return error;
  }
  KLOG_INFO("VGPU", "virtio-gpu selftest: rectangle flushed");
  return 0;
}
