// Glue embarqueur Skia pour ALOS (fournit ce que Skia attend de l'hote).
#include <stdarg.h>
#include <stdio.h>

#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"

// Journal Skia -> sortie standard ALOS.
void SkDebugf(const char format[], ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
}

// SkWriteBuffer reference toujours l'encodeur PNG. Le profil actuel n'embarque
// ni libpng (visibilite Chromium) ni le PNG Rust : l'encodage PNG est donc
// explicitement NON supporte et echoue (retourne false), il ne simule pas un
// succes. A remplacer par skia_use_rust_png_encode dans le profil Rust.
namespace SkPngEncoder {
bool Encode(SkWStream*, const SkPixmap&, const Options&) { return false; }
}  // namespace SkPngEncoder
