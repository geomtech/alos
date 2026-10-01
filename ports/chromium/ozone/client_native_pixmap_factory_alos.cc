#include "ui/ozone/common/stub_client_native_pixmap_factory.h"

namespace ui {

gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryAlos() {
  return CreateStubClientNativePixmapFactory();
}

}  // namespace ui