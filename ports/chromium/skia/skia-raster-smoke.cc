// Smoke Skia raster CPU sur ALOS : SkBitmap N32 + SkCanvas, rectangle, cercle
// anti-aliase, degrade lineaire et chemin. Verifie des pixels attendus, puis
// (si un virtio-gpu 2D est present et que ce processus est le display owner)
// copie le bitmap vers la surface SYS_GPU_MAP et flushe.
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
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
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradientShader.h"
#include "include/ports/SkFontMgr_directory.h"

static int g_failures;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("skia-raster-smoke: FAIL %s (line %d)\n", msg, __LINE__); \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

int main() {
  const int W = 320, H = 260;
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

  // Texte : police TrueType lue dans /fonts (SkFontMgr_New_Custom_Directory +
  // FreeType), glyphes rasterises anti-alias. Sans police, le test echoue.
  {
    // Diagnostic : le repertoire est-il lisible par la libc ALOS ?
    DIR* dir = opendir("/fonts");
    int entries = 0;
    if (dir) {
      while (readdir(dir)) entries++;
      closedir(dir);
    }
    FILE* f = fopen("/fonts/system_font.ttf", "rb");
    long size = -1;
    if (f) {
      fseek(f, 0, SEEK_END);
      size = ftell(f);
      fclose(f);
    }
    printf("skia-raster-smoke: /fonts opendir=%d entries=%d ttf_size=%ld\n", dir != nullptr, entries, size);
  }
  sk_sp<SkFontMgr> fonts = SkFontMgr_New_Custom_Directory("/fonts");
  printf("skia-raster-smoke: fontmgr=%d families=%d\n", fonts != nullptr, fonts ? fonts->countFamilies() : -1);
  sk_sp<SkTypeface> typeface;
  if (fonts && fonts->countFamilies() > 0) {
    SkString family;
    fonts->getFamilyName(0, &family);
    printf("skia-raster-smoke: family0=%s\n", family.c_str());
    typeface = fonts->matchFamilyStyle(family.c_str(), SkFontStyle());
  }
  CHECK(typeface != nullptr, "typeface from /fonts");
  if (typeface) {
    SkFont font(typeface, 36);
    font.setEdging(SkFont::Edging::kAntiAlias);
    paint.setColor(SK_ColorBLACK);
    canvas.drawString("ALOS Skia", 10, 235, font, paint);
  }

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
  {
    // Zone du texte [10..250]x[205..245] : pixels non blancs et niveaux de gris
    // intermediaires (anti-aliasing des glyphes).
    int dark = 0, grey = 0;
    for (int y = 205; y < 245; y++)
      for (int x = 10; x < 250; x++) {
        uint32_t c = px(x, y);
        if (c != SK_ColorWHITE) dark++;
        uint8_t r = SkColorGetR(c);
        if (r > 40 && r < 215) grey++;
      }
    printf("skia-raster-smoke: text pixels=%d antialiased=%d\n", dark, grey);
    CHECK(dark > 300, "text drawn");
    CHECK(grey > 40, "text antialiased");
  }
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
