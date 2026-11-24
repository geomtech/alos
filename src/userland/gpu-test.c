/* Test userland virtio-gpu 2D : le display owner mappe la surface, dessine un
 * degrade, un rectangle vert et un carre jaune, puis flushe. L'hote verifie
 * ensuite le scanout par screendump (test-vm.ps1 -GpuTest). */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/display.h>
#include <sys/gpu.h>

int main(void) {
  if (display_acquire() != 0) { puts("gpu-test: FAIL display_acquire"); return 1; }
  alos_gpu_info_t info;
  if (gpu_map(&info) != 0) { printf("gpu-test: FAIL gpu_map errno=%d\n", errno); return 1; }
  printf("gpu-test: surface %ux%u pitch=%u\n", info.width, info.height, info.pitch);
  volatile uint32_t *px = (volatile uint32_t *)(uintptr_t)info.addr;
  uint32_t stride = info.pitch / 4;
  for (uint32_t y = 0; y < info.height; y++)
    for (uint32_t x = 0; x < info.width; x++)
      px[y * stride + x] = (x * 255 / info.width) << 8 | (y * 255 / info.height); /* G=x, B=y */
  for (uint32_t y = 100; y < 200; y++)
    for (uint32_t x = 100; x < 400; x++) px[y * stride + x] = 0x0000FF00; /* vert */
  for (uint32_t y = 300; y < 340; y++)
    for (uint32_t x = 500; x < 540; x++) px[y * stride + x] = 0x00FFFF00; /* jaune */
  if (gpu_flush(0, 0, info.width, info.height) != 0) {
    printf("gpu-test: FAIL gpu_flush errno=%d\n", errno);
    return 1;
  }
  /* Rejets attendus : rectangle hors surface. */
  if (gpu_flush(info.width, 0, 1, 1) != -1 || errno != EINVAL) {
    puts("gpu-test: FAIL out-of-range flush accepted");
    return 1;
  }
  puts("gpu-test: PASS");
  display_release();
  return 0;
}
