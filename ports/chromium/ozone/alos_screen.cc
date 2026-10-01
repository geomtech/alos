#include "alos_screen.h"

#include "alos_window.h"
#include "alos_window_manager.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "ui/display/display.h"
#include "ui/display/util/display_util.h"
#include "ui/gfx/switches.h"

namespace ui {

namespace {

constexpr int64_t kAlosDisplayId = 1;
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 768;

bool ParseScreenSize(const std::string& text, int* width, int* height) {
  std::vector<std::string_view> parts = base::SplitStringPiece(
      text, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  return parts.size() == 2 && base::StringToInt(parts[0], width) &&
         base::StringToInt(parts[1], height) && *width > 0 && *height > 0;
}

gfx::Rect InitialBounds() {
  int width = kDefaultWidth;
  int height = kDefaultHeight;
  const base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line && command_line->HasSwitch(switches::kOzoneOverrideScreenSize)) {
    ParseScreenSize(command_line->GetSwitchValueASCII(
                        switches::kOzoneOverrideScreenSize),
                    &width, &height);
  }
  return gfx::Rect(width, height);
}

}  // namespace

AlosScreen::AlosScreen(AlosWindowManager* manager) : manager_(manager) {
  display::Display display(kAlosDisplayId);
  display.SetScaleAndBounds(1.0f, InitialBounds());
  display_list_.AddDisplay(display, display::DisplayList::Type::PRIMARY);
}

AlosScreen::~AlosScreen() = default;

const std::vector<display::Display>& AlosScreen::GetAllDisplays() const {
  return display_list_.displays();
}

display::Display AlosScreen::GetPrimaryDisplay() const {
  auto it = display_list_.GetPrimaryDisplayIterator();
  CHECK(it != display_list_.displays().end());
  return *it;
}

display::Display AlosScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  if (AlosWindow* window = manager_->GetWindow(widget))
    return GetDisplayMatching(window->GetBoundsInPixels());
  return GetPrimaryDisplay();
}

gfx::Point AlosScreen::GetCursorScreenPoint() const {
  return gfx::Point();
}

gfx::AcceleratedWidget AlosScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point_in_dip) const {
  return manager_->GetAcceleratedWidgetAtScreenPoint(point_in_dip);
}

display::Display AlosScreen::GetDisplayNearestPoint(
    const gfx::Point& point_in_dip) const {
  for (const auto& display : display_list_.displays()) {
    if (display.bounds().Contains(point_in_dip))
      return display;
  }
  return GetPrimaryDisplay();
}

display::Display AlosScreen::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  if (auto display = display::FindDisplayWithBiggestIntersection(
          display_list_.displays(), match_rect)) {
    return *display;
  }
  return GetPrimaryDisplay();
}

void AlosScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void AlosScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

}  // namespace ui