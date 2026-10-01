#ifndef ALOS_NATIVE_OZONE_ALOS_SURFACE_FACTORY_H_
#define ALOS_NATIVE_OZONE_ALOS_SURFACE_FACTORY_H_

#include "base/memory/raw_ptr.h"
#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

class AlosWindowManager;

class AlosSurfaceFactory : public SurfaceFactoryOzone {
 public:
  explicit AlosSurfaceFactory(AlosWindowManager* manager);
  AlosSurfaceFactory(const AlosSurfaceFactory&) = delete;
  AlosSurfaceFactory& operator=(const AlosSurfaceFactory&) = delete;
  ~AlosSurfaceFactory() override;

  std::vector<gl::GLImplementationParts> GetAllowedGLImplementations() override;
  GLOzone* GetGLOzone(const gl::GLImplementationParts& implementation) override;
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;

 private:
  raw_ptr<AlosWindowManager> manager_ = nullptr;
};

}  // namespace ui

#endif  // ALOS_NATIVE_OZONE_ALOS_SURFACE_FACTORY_H_