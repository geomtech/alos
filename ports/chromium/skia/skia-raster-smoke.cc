// Smoke Skia raster CPU sur ALOS : SkBitmap N32 + SkCanvas, rectangle, cercle
// anti-aliase, degrade lineaire et chemin. Verifie des pixels attendus, puis
// (si un virtio-gpu 2D est present et que ce processus est le display owner)
// copie le bitmap vers la surface SYS_GPU_MAP et flushe.
#include <stdio.h>
#include <string.h>
extern "C" {
#include <sys/display.h>
}
#include <sys/gpu.h>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkShader.h"
#include "include/effects/SkGradientShader.h"

static int g_failures;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("skia-raster-smoke: FAIL %s (line %d)\n", msg, __LINE__); \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

int main() {
  const int W = 320, H = 200;
  SkBitmap bitmap;
  // BGRA explicite : meme ordre memoire que la surface virtio-gpu (0x00RRGGBB LE),
  // independant du N32 par defaut de la plateforme.
  if (!bitmap.tryAllocPixels(SkImageInfo::Make(W, H, kBGRA_8888_SkColorType,
                                               kPremul_SkAlphaType))) {
    puts("skia-raster-smoke: FAIL alloc");
    return 1;
  }
  SkCanvas canvas(bitmap);
  canvas.clear(SK_ColorWHITE);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SkColorSetRGB(0x20, 0x40, 0xE0));
  canvas.drawRect(SkRect::MakeXYWH(10, 10, 100, 60), paint);

  SkPoint pts[2] = {{120, 0}, {220, 0}};
  SkColor colors[2] = {SK_ColorRED, SK_ColorYELLOW};
  paint.setShader(SkGradientShader::MakeLinear(pts, colors, nullptr, 2,
                                                SkTileMode::kClamp));
  canvas.drawRect(SkRect::MakeXYWH(120, 10, 100, 60), paint);
  paint.setShader(nullptr);

  paint.setColor(SK_ColorBLACK);
  canvas.drawCircle(60, 140, 40, paint);

  SkPath path;
  path.moveTo(240, 190);
  path.lineTo(310, 190);
  path.lineTo(275, 100);
  path.close();
  paint.setColor(SkColorSetRGB(0x10, 0xA0, 0x30));
  canvas.drawPath(path, paint);

  auto px = [&](int x, int y) { return (uint32_t)bitmap.getColor(x, y); };
  CHECK(px(5, 5) == SK_ColorWHITE, "background white");
  CHECK(px(50, 40) == SkColorSetRGB(0x20, 0x40, 0xE0), "blue rect");
  uint32_t left = px(125, 40), right = px(215, 40);
  CHECK(SkColorGetR(left) > 200 && SkColorGetG(left) < 80, "gradient start red");
  CHECK(SkColorGetG(right) > 180 && SkColorGetR(right) > 200, "gradient end yellow");
  CHECK(SkColorGetG(px(170, 40)) > SkColorGetG(left) &&
            SkColorGetG(px(170, 40)) < SkColorGetG(right), "gradient monotonic");
  CHECK(px(60, 140) == SK_ColorBLACK, "circle center");
  // Bord du cercle (rayon 40 centre en 60,140) : au moins un pixel a couverture
  // partielle (ni noir ni blanc pur) prouve l'anti-aliasing.
  bool partial = false;
  for (int x = 95; x <= 105; x++) {
    uint32_t c = px(x, 140);
    if (c != SK_ColorWHITE && c != SK_ColorBLACK) partial = true;
  }
  CHECK(partial, "circle edge antialiased");
  CHECK(px(275, 160) == SkColorSetRGB(0x10, 0xA0, 0x30), "triangle fill");

  if (g_failures) return 1;
  printf("skia-raster-smoke: PASS %dx%d\n", W, H);

  // Presentation optionnelle sur virtio-gpu (display owner requis).
  alos_gpu_info_t info;
  if (display_acquire() == 0 && gpu_map(&info) == 0) {
    uint32_t* dst = reinterpret_cast<uint32_t*>(info.addr);
    for (uint32_t y = 0; y < info.height; y++)
      for (uint32_t x = 0; x < info.width; x++) dst[y * (info.pitch / 4) + x] = 0xFF303030;
    for (int y = 0; y < H && (uint32_t)y < info.height; y++)
      memcpy(dst + (size_t)(y + 20) * (info.pitch / 4) + 20, bitmap.getAddr32(0, y),
             W * sizeof(uint32_t));
    if (gpu_flush(0, 0, info.width, info.height) == 0)
      puts("skia-raster-smoke: presented via virtio-gpu");
  }
  return 0;
}
