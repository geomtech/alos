// Compile/link probe for the Chromium 140 software Ozone contract.
#include <gui.h>

#include <memory>
#include <type_traits>

#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/vsync_provider.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace {

class AlosSurfaceProbe final : public ui::SurfaceOzoneCanvas {
 public:
  explicit AlosSurfaceProbe(gui_window_t* window) : window_(window) {}

  SkCanvas* GetCanvas() override {
    return surface_ ? surface_->getCanvas() : nullptr;
  }

  void ResizeCanvas(const gfx::Size& viewport_size, float scale) override {
    surface_.reset();
    if (scale <= 0.0f || viewport_size.width() <= 0 ||
        viewport_size.height() <= 0 ||
        static_cast<uint32_t>(viewport_size.width()) !=
            gui_window_width(window_) ||
        static_cast<uint32_t>(viewport_size.height()) !=
            gui_window_height(window_)) {
      return;
    }

    SkImageInfo info =
        SkImageInfo::MakeN32Premul(viewport_size.width(),
                                  viewport_size.height());
    surface_ = SkSurfaces::WrapPixels(
        info, gui_window_pixels(window_),
        static_cast<size_t>(gui_window_stride(window_)) * sizeof(uint32_t));
  }

  void PresentCanvas(const gfx::Rect& damage) override {
    if (!surface_) {
      return;
    }
    gui_rect_t rect = {
        damage.x(),
        damage.y(),
        static_cast<uint32_t>(damage.width()),
        static_cast<uint32_t>(damage.height()),
    };
    if (gui_present(window_, &rect) != 0) {
      surface_.reset();
    }
  }

  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override {
    return nullptr;
  }

 private:
  gui_window_t* window_;
  sk_sp<SkSurface> surface_;
};

static_assert(std::is_base_of_v<ui::SurfaceOzoneCanvas, AlosSurfaceProbe>);
static_assert(
    std::is_same_v<decltype(&ui::SurfaceOzoneCanvas::PresentCanvas),
                   void (ui::SurfaceOzoneCanvas::*)(const gfx::Rect&)>);

}  // namespace

int main() {
  gui_window_t* window =
      gui_create_window("Ozone interface probe", 320, 200, GUI_WINDOW_DEFAULT);
  if (!window) {
    return 1;
  }

  AlosSurfaceProbe surface(window);
  surface.ResizeCanvas(gfx::Size(320, 200), 1.0f);
  SkCanvas* canvas = surface.GetCanvas();
  if (!canvas) {
    gui_destroy_window(window);
    return 2;
  }
  canvas->clear(SK_ColorBLACK);
  surface.PresentCanvas(gfx::Rect(320, 200));
  if (!surface.GetCanvas()) {
    gui_destroy_window(window);
    return 3;
  }
  gui_destroy_window(window);
  return 0;
}
