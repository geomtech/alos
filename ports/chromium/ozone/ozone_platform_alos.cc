#include <gui.h>

#include <memory>

#include "alos_screen.h"
#include "alos_surface_factory.h"
#include "alos_window.h"
#include "alos_window_manager.h"
#include "base/memory/no_destructor.h"
#include "base/memory/raw_ptr.h"
#include "base/notimplemented.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/task/current_thread.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_checker.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {

namespace {

// Les evenements libgui sont lus sur le thread UI : libgui n'est pas
// thread-safe (file d'evenements, reponses de creation et remappage des
// surfaces au redimensionnement partagent un etat global). Le descripteur
// IPC du desktop est pollable et surveille par la pompe MessagePumpAlos.
class AlosPlatformEventSource final
    : public PlatformEventSource,
      public base::MessagePumpForUI::FdWatcher {
 public:
  explicit AlosPlatformEventSource(AlosWindowManager* manager)
      : manager_(manager), controller_(FROM_HERE) {}

  AlosPlatformEventSource(const AlosPlatformEventSource&) = delete;
  AlosPlatformEventSource& operator=(const AlosPlatformEventSource&) = delete;

  ~AlosPlatformEventSource() override { controller_.StopWatchingFileDescriptor(); }

  // Appele apres chaque creation de fenetre : la connexion libgui n'existe
  // qu'a partir de la premiere fenetre.
  void StartWatching() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    if (watching_)
      return;
    int fd = gui_connection_fd();
    if (fd < 0 || !base::CurrentUIThread::IsSet())
      return;
    watching_ = base::CurrentUIThread::Get()->WatchFileDescriptor(
        fd, /*persistent=*/true, base::MessagePumpForUI::WATCH_READ,
        &controller_, this);
    // Des evenements ont pu etre mis en file pendant gui_create_window.
    DrainEvents();
  }

  void OnFileCanReadWithoutBlocking(int fd) override { DrainEvents(); }
  void OnFileCanWriteWithoutBlocking(int fd) override {}

 private:
  void DrainEvents() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    // Borne pour ne pas affamer les autres taches de la boucle UI ; poll
    // reste pret tant que des messages sont en file.
    for (int i = 0; i < 64; ++i) {
      gui_event_t event;
      int result = gui_poll_event(nullptr, &event);
      if (result < 0) {
        // Desktop ferme : arreter la surveillance plutot que boucler.
        controller_.StopWatchingFileDescriptor();
        watching_ = false;
        return;
      }
      if (result == 0)
        return;
      if (AlosWindow* window = manager_->GetWindowForNative(event.window))
        window->DispatchGuiEvent(event);
    }
  }

  raw_ptr<AlosWindowManager> manager_ = nullptr;
  base::MessagePumpForUI::FdWatchController controller_;
  bool watching_ = false;
  THREAD_CHECKER(thread_checker_);
};
class OzonePlatformAlos final : public OzonePlatform {
 public:
  OzonePlatformAlos() = default;
  OzonePlatformAlos(const OzonePlatformAlos&) = delete;
  OzonePlatformAlos& operator=(const OzonePlatformAlos&) = delete;
  ~OzonePlatformAlos() override = default;

  SurfaceFactoryOzone* GetSurfaceFactoryOzone() override {
    return surface_factory_.get();
  }
  OverlayManagerOzone* GetOverlayManager() override {
    return overlay_manager_.get();
  }
  CursorFactory* GetCursorFactory() override { return cursor_factory_.get(); }
  InputController* GetInputController() override {
    return input_controller_.get();
  }
  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_support_host_.get();
  }
  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }
  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    if (!window_manager_)
      return nullptr;
    auto window = AlosWindow::Create(delegate, window_manager_.get(),
                                     properties.bounds, properties.title);
    if (event_source_)
      event_source_->StartWatching();
    return window;
  }
  bool IsWindowCompositingSupported() const override { return true; }
  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }
  std::unique_ptr<PlatformScreen> CreateScreen() override {
    return std::make_unique<AlosScreen>(window_manager_.get());
  }
  void InitScreen(PlatformScreen* screen) override {}
  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* ime_key_event_dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(ime_key_event_dispatcher);
  }
  const PlatformProperties& GetPlatformProperties() override {
    static base::NoDestructor<OzonePlatform::PlatformProperties> properties;
    return *properties;
  }

  void PostCreateMainMessageLoop(
      base::OnceCallback<void()> shutdown_cb,
      scoped_refptr<base::SingleThreadTaskRunner> user_input_task_runner)
      override {
    // Les evenements sont deja distribues sur le thread UI par la pompe.
  }

 private:
  bool InitializeUI(const InitParams& params) override {
    window_manager_ = std::make_unique<AlosWindowManager>();
    surface_factory_ = std::make_unique<AlosSurfaceFactory>(window_manager_.get());
    if (!PlatformEventSource::GetInstance())
      event_source_ = std::make_unique<AlosPlatformEventSource>(window_manager_.get());
    keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
        keyboard_layout_engine_.get());
    overlay_manager_ = std::make_unique<StubOverlayManager>();
    input_controller_ = std::make_unique<StubInputController>();
    cursor_factory_ = std::make_unique<BitmapCursorFactory>();
    gpu_platform_support_host_.reset(CreateStubGpuPlatformSupportHost());
    return true;
  }

  void InitializeGPU(const InitParams& params) override {
    if (!window_manager_)
      window_manager_ = std::make_unique<AlosWindowManager>();
    if (!surface_factory_)
      surface_factory_ = std::make_unique<AlosSurfaceFactory>(window_manager_.get());
  }

  std::unique_ptr<KeyboardLayoutEngine> keyboard_layout_engine_;
  std::unique_ptr<AlosWindowManager> window_manager_;
  std::unique_ptr<AlosSurfaceFactory> surface_factory_;
  std::unique_ptr<AlosPlatformEventSource> event_source_;
  std::unique_ptr<CursorFactory> cursor_factory_;
  std::unique_ptr<InputController> input_controller_;
  std::unique_ptr<GpuPlatformSupportHost> gpu_platform_support_host_;
  std::unique_ptr<OverlayManagerOzone> overlay_manager_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformAlos() {
  return new OzonePlatformAlos;
}

}  // namespace ui