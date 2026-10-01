#include "alos_window.h"

#include <algorithm>

#include "alos_window_manager.h"
#include "base/notimplemented.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "ui/display/types/display_constants.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace ui {

namespace {

constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 600;

KeyboardCode KeyboardCodeFromGui(uint32_t key, uint32_t scancode) {
  if (key >= 'a' && key <= 'z')
    return static_cast<KeyboardCode>(VKEY_A + (key - 'a'));
  if (key >= 'A' && key <= 'Z')
    return static_cast<KeyboardCode>(VKEY_A + (key - 'A'));
  if (key >= '0' && key <= '9')
    return static_cast<KeyboardCode>(VKEY_0 + (key - '0'));
  switch (key) {
    case 8:
      return VKEY_BACK;
    case 9:
      return VKEY_TAB;
    case 13:
      return VKEY_RETURN;
    case 27:
      return VKEY_ESCAPE;
    case ' ':
      return VKEY_SPACE;
    default:
      break;
  }
  switch (scancode & 0x7f) {
    case 0x01:
      return VKEY_ESCAPE;
    case 0x0e:
      return VKEY_BACK;
    case 0x0f:
      return VKEY_TAB;
    case 0x1c:
      return VKEY_RETURN;
    case 0x2a:
    case 0x36:
      return VKEY_SHIFT;
    case 0x1d:
      return VKEY_CONTROL;
    case 0x38:
      return VKEY_MENU;
    default:
      return VKEY_UNKNOWN;
  }
}

}  // namespace

std::unique_ptr<AlosWindow> AlosWindow::Create(
    PlatformWindowDelegate* delegate,
    AlosWindowManager* manager,
    const gfx::Rect& requested_bounds,
    const std::u16string& title) {
  gfx::Rect bounds = requested_bounds;
  if (bounds.width() <= 0 || bounds.height() <= 0)
    bounds.set_size(gfx::Size(kDefaultWidth, kDefaultHeight));

  std::string utf8_title = base::UTF16ToUTF8(title);
  if (utf8_title.empty())
    utf8_title = "Chromium";
  gui_window_t* native = gui_create_window(
      utf8_title.c_str(), static_cast<uint32_t>(bounds.width()),
      static_cast<uint32_t>(bounds.height()), GUI_WINDOW_DEFAULT);
  if (!native)
    return nullptr;

  auto window = std::unique_ptr<AlosWindow>(
      new AlosWindow(delegate, manager, native, bounds));
  window->widget_ = manager->AddWindow(window.get(), native);
  delegate->OnAcceleratedWidgetAvailable(window->widget_);
  return window;
}

AlosWindow::AlosWindow(PlatformWindowDelegate* delegate,
                       AlosWindowManager* manager,
                       gui_window_t* native_window,
                       const gfx::Rect& bounds)
    : delegate_(delegate), manager_(manager), native_window_(native_window),
      bounds_(bounds) {}

AlosWindow::~AlosWindow() {
  if (widget_ != gfx::kNullAcceleratedWidget)
    delegate_->OnWillDestroyAcceleratedWidget();
  manager_->RemoveWindow(widget_, native_window_);
  if (native_window_)
    gui_destroy_window(native_window_);
}

void AlosWindow::Show(bool inactive) {
  visible_ = true;
  if (!inactive)
    Activate();
}

void AlosWindow::Hide() {
  visible_ = false;
  Deactivate();
}

void AlosWindow::Close() {
  delegate_->OnClosed();
}

bool AlosWindow::IsVisible() const {
  return visible_;
}

void AlosWindow::PrepareForShutdown() {}

void AlosWindow::SetBoundsInPixels(const gfx::Rect& bounds) {
  if (bounds.size() != bounds_.size()) {
    NOTIMPLEMENTED_LOG_ONCE() << "ALOS libgui has no client resize request API";
    return;
  }
  bool origin_changed = bounds.origin() != bounds_.origin();
  bounds_ = bounds;
  delegate_->OnBoundsChanged({origin_changed});
}

gfx::Rect AlosWindow::GetBoundsInPixels() const {
  return bounds_;
}

void AlosWindow::SetBoundsInDIP(const gfx::Rect& bounds) {
  SetBoundsInPixels(delegate_->ConvertRectToPixels(bounds));
}

gfx::Rect AlosWindow::GetBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(bounds_);
}

void AlosWindow::SetTitle(const std::u16string& title) {
  std::string utf8_title = base::UTF16ToUTF8(title);
  if (native_window_ && gui_set_title(native_window_, utf8_title.c_str()) != 0)
    NOTIMPLEMENTED_LOG_ONCE() << "ALOS title update failed";
}

void AlosWindow::SetCapture() {
  capture_ = true;
}

void AlosWindow::ReleaseCapture() {
  capture_ = false;
  delegate_->OnLostCapture();
}

bool AlosWindow::HasCapture() const {
  return capture_;
}

void AlosWindow::SetFullscreen(bool fullscreen, int64_t target_display_id) {
  if (target_display_id != display::kInvalidDisplayId)
    NOTIMPLEMENTED_LOG_ONCE();
  UpdateWindowState(fullscreen ? PlatformWindowState::kFullScreen
                               : PlatformWindowState::kNormal);
}

void AlosWindow::Maximize() {
  UpdateWindowState(PlatformWindowState::kMaximized);
}

void AlosWindow::Minimize() {
  UpdateWindowState(PlatformWindowState::kMinimized);
  Deactivate();
}

void AlosWindow::Restore() {
  UpdateWindowState(PlatformWindowState::kNormal);
}

PlatformWindowState AlosWindow::GetPlatformWindowState() const {
  return window_state_;
}

void AlosWindow::Activate() {
  delegate_->OnActivationChanged(true);
}

void AlosWindow::Deactivate() {
  delegate_->OnActivationChanged(false);
}

void AlosWindow::SetUseNativeFrame(bool use_native_frame) {}

bool AlosWindow::ShouldUseNativeFrame() const {
  return true;
}

void AlosWindow::SetCursor(scoped_refptr<PlatformCursor> cursor) {}
void AlosWindow::MoveCursorTo(const gfx::Point& location) {}
void AlosWindow::ConfineCursorToBounds(const gfx::Rect& bounds) {}

void AlosWindow::SetRestoredBoundsInDIP(const gfx::Rect& bounds) {
  restored_bounds_ = delegate_->ConvertRectToPixels(bounds);
}

gfx::Rect AlosWindow::GetRestoredBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(restored_bounds_.value_or(bounds_));
}

void AlosWindow::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                const gfx::ImageSkia& app_icon) {}

void AlosWindow::SizeConstraintsChanged() {}

void AlosWindow::DispatchGuiEvent(const gui_event_t& event) {
  switch (event.type) {
    case GUI_EVENT_PAINT:
      delegate_->OnDamageRect(gfx::Rect(bounds_.size()));
      break;
    case GUI_EVENT_RESIZE:
      UpdateBoundsFromGui(event.width, event.height);
      break;
    case GUI_EVENT_FOCUS:
      delegate_->OnActivationChanged(event.focused != 0);
      break;
    case GUI_EVENT_MOUSE_MOVE: {
      gfx::Point point(event.x, event.y);
      MouseEvent mouse(EventType::kMouseMoved, point, bounds_.origin() + point,
                       base::TimeTicks::Now(),
                       EventFlagsFromGui(event.buttons, event.modifiers), 0);
      delegate_->DispatchEvent(&mouse);
      last_buttons_ = event.buttons;
      break;
    }
    case GUI_EVENT_MOUSE_DOWN:
    case GUI_EVENT_MOUSE_UP: {
      bool pressed = event.type == GUI_EVENT_MOUSE_DOWN;
      int changed = ChangedButtonFlags(event.buttons, pressed);
      gfx::Point point(event.x, event.y);
      MouseEvent mouse(pressed ? EventType::kMousePressed
                               : EventType::kMouseReleased,
                       point, bounds_.origin() + point, base::TimeTicks::Now(),
                       EventFlagsFromGui(event.buttons, event.modifiers),
                       changed);
      delegate_->DispatchEvent(&mouse);
      last_buttons_ = event.buttons;
      break;
    }
    case GUI_EVENT_KEY_DOWN:
    case GUI_EVENT_KEY_UP: {
      KeyEvent key(event.type == GUI_EVENT_KEY_DOWN ? EventType::kKeyPressed
                                                    : EventType::kKeyReleased,
                   KeyboardCodeFromGui(event.key, event.scancode),
                   DomCode::NONE, EventFlagsFromGui(0, event.modifiers),
                   base::TimeTicks::Now());
      delegate_->DispatchEvent(&key);
      break;
    }
    case GUI_EVENT_CLOSE:
      delegate_->OnCloseRequest();
      break;
    case GUI_EVENT_NONE:
      break;
  }
}

void AlosWindow::UpdateBoundsFromGui(uint32_t width, uint32_t height) {
  gfx::Size size(static_cast<int>(width), static_cast<int>(height));
  if (size == bounds_.size())
    return;
  bounds_.set_size(size);
  delegate_->OnBoundsChanged({false});
}

void AlosWindow::UpdateWindowState(PlatformWindowState new_state) {
  if (window_state_ == new_state)
    return;
  PlatformWindowState old_state = window_state_;
  window_state_ = new_state;
  delegate_->OnWindowStateChanged(old_state, new_state);
}

int AlosWindow::EventFlagsFromGui(uint32_t buttons, uint32_t modifiers) const {
  int flags = EF_NONE;
  if (buttons & 1U)
    flags |= EF_LEFT_MOUSE_BUTTON;
  if (buttons & 2U)
    flags |= EF_RIGHT_MOUSE_BUTTON;
  if (buttons & 4U)
    flags |= EF_MIDDLE_MOUSE_BUTTON;
  if (modifiers & 1U)
    flags |= EF_SHIFT_DOWN;
  if (modifiers & 2U)
    flags |= EF_CONTROL_DOWN;
  if (modifiers & 4U)
    flags |= EF_ALT_DOWN;
  return flags;
}

int AlosWindow::ChangedButtonFlags(uint32_t buttons, bool pressed) {
  uint32_t changed = pressed ? (buttons & ~last_buttons_) : (last_buttons_ & ~buttons);
  if (changed & 1U)
    return EF_LEFT_MOUSE_BUTTON;
  if (changed & 2U)
    return EF_RIGHT_MOUSE_BUTTON;
  if (changed & 4U)
    return EF_MIDDLE_MOUSE_BUTTON;
  return EventFlagsFromGui(buttons, 0) & EF_MOUSE_BUTTON;
}

}  // namespace ui