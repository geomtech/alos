#include "alos_surface_factory.h"

#include <gui.h>

#include "alos_window.h"
#include "alos_window_manager.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gfx/vsync_provider.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace ui {

namespace {

class AlosCanvasSurface final : public SurfaceOzoneCanvas {
 public:
  AlosCanvasSurface(gfx::AcceleratedWidget widget, AlosWindowManager* manager)
      : widget_(widget), manager_(manager) {}

  void ResizeCanvas(const gfx::Size& viewport_size, float scale) override {
    surface_.reset();
    if (scale <= 0.0f || viewport_size.width() <= 0 ||
        viewport_size.height() <= 0) {
      return;
    }
    AlosWindow* window = manager_->GetWindow(widget_);
    if (!window || !window->native_window())
      return;
    gui_window_t* native = window->native_window();
    if (static_cast<uint32_t>(viewport_size.width()) !=
            gui_window_width(native) ||
        static_cast<uint32_t>(viewport_size.height()) !=
            gui_window_height(native)) {
      return;
    }
    SkImageInfo info =
        SkImageInfo::MakeN32Premul(viewport_size.width(), viewport_size.height());
    surface_ = SkSurfaces::WrapPixels(
        info, gui_window_pixels(native),
        static_cast<size_t>(gui_window_stride(native)) * sizeof(uint32_t));
  }

  SkCanvas* GetCanvas() override {
    return surface_ ? surface_->getCanvas() : nullptr;
  }

  void PresentCanvas(const gfx::Rect& damage) override {
    AlosWindow* window = manager_->GetWindow(widget_);
    if (!window || !window->native_window() || !surface_)
      return;
    if (damage.IsEmpty()) {
      if (gui_present(window->native_window(), nullptr) != 0)
        surface_.reset();
      return;
    }
    gui_rect_t rect = {damage.x(), damage.y(),
                       static_cast<uint32_t>(damage.width()),
                       static_cast<uint32_t>(damage.height())};
    if (gui_present(window->native_window(), &rect) != 0)
      surface_.reset();
  }

  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override {
    return nullptr;
  }

 private:
  gfx::AcceleratedWidget widget_;
  raw_ptr<AlosWindowManager> manager_ = nullptr;
  sk_sp<SkSurface> surface_;
};

}  // namespace

AlosSurfaceFactory::AlosSurfaceFactory(AlosWindowManager* manager)
    : manager_(manager) {}
AlosSurfaceFactory::~AlosSurfaceFactory() = default;

std::vector<gl::GLImplementationParts>
AlosSurfaceFactory::GetAllowedGLImplementations() {
  return {};
}

GLOzone* AlosSurfaceFactory::GetGLOzone(
    const gl::GLImplementationParts& implementation) {
  return nullptr;
}

std::unique_ptr<SurfaceOzoneCanvas> AlosSurfaceFactory::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  return std::make_unique<AlosCanvasSurface>(widget, manager_);
}

}  // namespace ui