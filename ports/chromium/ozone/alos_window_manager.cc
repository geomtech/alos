#include "alos_window_manager.h"

#include "alos_window.h"

namespace ui {

AlosWindowManager::AlosWindowManager() = default;
AlosWindowManager::~AlosWindowManager() = default;

gfx::AcceleratedWidget AlosWindowManager::AddWindow(AlosWindow* window,
                                                    gui_window_t* native) {
  base::AutoLock lock(lock_);
  gfx::AcceleratedWidget widget = windows_.Add(window);
  native_windows_[native] = window;
  return widget;
}

void AlosWindowManager::RemoveWindow(gfx::AcceleratedWidget widget,
                                     gui_window_t* native) {
  base::AutoLock lock(lock_);
  windows_.Remove(widget);
  native_windows_.erase(native);
}

AlosWindow* AlosWindowManager::GetWindow(gfx::AcceleratedWidget widget) {
  base::AutoLock lock(lock_);
  return windows_.Lookup(widget);
}

AlosWindow* AlosWindowManager::GetWindowForNative(gui_window_t* native) {
  base::AutoLock lock(lock_);
  auto it = native_windows_.find(native);
  return it == native_windows_.end() ? nullptr : it->second.get();
}

gfx::AcceleratedWidget AlosWindowManager::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) {
  base::AutoLock lock(lock_);
  for (base::IDMap<AlosWindow*>::const_iterator it(&windows_); !it.IsAtEnd();
       it.Advance()) {
    AlosWindow* window = it.GetCurrentValue();
    if (window && window->GetBoundsInPixels().Contains(point)) {
      return it.GetCurrentKey();
    }
  }
  return gfx::kNullAcceleratedWidget;
}

}  // namespace ui