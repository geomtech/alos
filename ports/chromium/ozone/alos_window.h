#ifndef ALOS_NATIVE_OZONE_ALOS_WINDOW_H_
#define ALOS_NATIVE_OZONE_ALOS_WINDOW_H_

#include <optional>
#include <string>

#include <gui.h>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

class AlosWindowManager;

class AlosWindow : public PlatformWindow {
 public:
  static std::unique_ptr<AlosWindow> Create(PlatformWindowDelegate* delegate,
                                            AlosWindowManager* manager,
                                            const gfx::Rect& bounds,
                                            const std::u16string& title);

  AlosWindow(const AlosWindow&) = delete;
  AlosWindow& operator=(const AlosWindow&) = delete;
  ~AlosWindow() override;

  void Show(bool inactive) override;
  void Hide() override;
  void Close() override;
  bool IsVisible() const override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInDIP() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  bool HasCapture() const override;
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  PlatformWindowState GetPlatformWindowState() const override;
  void Activate() override;
  void Deactivate() override;
  void SetUseNativeFrame(bool use_native_frame) override;
  bool ShouldUseNativeFrame() const override;
  void SetCursor(scoped_refptr<PlatformCursor> cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  void SetRestoredBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetRestoredBoundsInDIP() const override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void SizeConstraintsChanged() override;

  void DispatchGuiEvent(const gui_event_t& event);
  gui_window_t* native_window() const { return native_window_; }
  gfx::AcceleratedWidget widget() const { return widget_; }

 private:
  AlosWindow(PlatformWindowDelegate* delegate,
             AlosWindowManager* manager,
             gui_window_t* native_window,
             const gfx::Rect& bounds);

  void UpdateBoundsFromGui(uint32_t width, uint32_t height);
  void UpdateWindowState(PlatformWindowState new_state);
  int EventFlagsFromGui(uint32_t buttons, uint32_t modifiers) const;
  int ChangedButtonFlags(uint32_t buttons, bool pressed);

  raw_ptr<PlatformWindowDelegate> delegate_ = nullptr;
  raw_ptr<AlosWindowManager> manager_ = nullptr;
  raw_ptr<gui_window_t> native_window_ = nullptr;
  gfx::AcceleratedWidget widget_ = gfx::kNullAcceleratedWidget;
  gfx::Rect bounds_;
  bool visible_ = false;
  bool capture_ = false;
  uint32_t last_buttons_ = 0;
  std::optional<gfx::Rect> restored_bounds_;
  PlatformWindowState window_state_ = PlatformWindowState::kNormal;
};

}  // namespace ui

#endif  // ALOS_NATIVE_OZONE_ALOS_WINDOW_H_