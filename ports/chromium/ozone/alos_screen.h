#ifndef ALOS_NATIVE_OZONE_ALOS_SCREEN_H_
#define ALOS_NATIVE_OZONE_ALOS_SCREEN_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/display/display_list.h"
#include "ui/ozone/public/platform_screen.h"

namespace ui {

class AlosWindowManager;

class AlosScreen : public PlatformScreen {
 public:
  explicit AlosScreen(AlosWindowManager* manager);
  AlosScreen(const AlosScreen&) = delete;
  AlosScreen& operator=(const AlosScreen&) = delete;
  ~AlosScreen() override;

  const std::vector<display::Display>& GetAllDisplays() const override;
  display::Display GetPrimaryDisplay() const override;
  display::Display GetDisplayForAcceleratedWidget(
      gfx::AcceleratedWidget widget) const override;
  gfx::Point GetCursorScreenPoint() const override;
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point_in_dip) const override;
  display::Display GetDisplayNearestPoint(
      const gfx::Point& point_in_dip) const override;
  display::Display GetDisplayMatching(const gfx::Rect& match_rect) const override;
  void AddObserver(display::DisplayObserver* observer) override;
  void RemoveObserver(display::DisplayObserver* observer) override;

 private:
  display::DisplayList display_list_;
  raw_ptr<AlosWindowManager> manager_ = nullptr;
};

}  // namespace ui

#endif  // ALOS_NATIVE_OZONE_ALOS_SCREEN_H_