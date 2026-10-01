// ALOS out-of-tree Ozone platform metadata.
#ifndef ALOS_NATIVE_OZONE_ALOS_WINDOW_MANAGER_H_
#define ALOS_NATIVE_OZONE_ALOS_WINDOW_MANAGER_H_

#include <map>

#include <gui.h>

#include "base/containers/id_map.h"
#include "base/memory/raw_ptr.h"
#include "base/synchronization/lock.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_widget_types.h"

namespace ui {

class AlosWindow;

class AlosWindowManager {
 public:
  AlosWindowManager();
  AlosWindowManager(const AlosWindowManager&) = delete;
  AlosWindowManager& operator=(const AlosWindowManager&) = delete;
  ~AlosWindowManager();

  gfx::AcceleratedWidget AddWindow(AlosWindow* window, gui_window_t* native);
  void RemoveWindow(gfx::AcceleratedWidget widget, gui_window_t* native);
  AlosWindow* GetWindow(gfx::AcceleratedWidget widget);
  AlosWindow* GetWindowForNative(gui_window_t* native);
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point);

 private:
  base::Lock lock_;
  base::IDMap<AlosWindow*> windows_ GUARDED_BY(lock_);
  std::map<gui_window_t*, raw_ptr<AlosWindow>> native_windows_
      GUARDED_BY(lock_);
};

}  // namespace ui

#endif  // ALOS_NATIVE_OZONE_ALOS_WINDOW_MANAGER_H_