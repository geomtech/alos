#include "alos_mojo/echo.mojom.h"
#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/process/launch.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/thread.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/buffer.h"
#include "mojo/public/cpp/system/invitation.h"
#include <optional>
#include <stdio.h>

class EchoService : public alos::mojom::Echo {
 public:
  unsigned exchanges = 0;
  bool finished = false;
  void Exchange(const std::string& text, mojo::ScopedSharedBufferHandle buffer,
                ExchangeCallback callback) override {
    CHECK_EQ(text, "typed request");
    CHECK(buffer.is_valid());
    CHECK_EQ(buffer->GetSize(), 4096U);
    auto mapping = buffer->Map(4096);
    CHECK(mapping);
    auto* bytes = static_cast<unsigned char*>(mapping.get());
    CHECK_EQ(bytes[19], 73);
    bytes[19] = 91;
    ++exchanges;
    std::move(callback).Run("typed response", bytes[19]);
  }
  void Finish(FinishCallback callback) override {
    CHECK_EQ(exchanges, 1U);
    finished = true;
    std::move(callback).Run();
  }
};

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  const auto& command = *base::CommandLine::ForCurrentProcess();
  const bool child = command.HasSwitch("child");
  std::optional<mojo::PlatformChannel> channel;
  base::Process process;
  if (!child) {
    channel.emplace();
    base::CommandLine child_command(
        base::FilePath("/bin/chromium-mojo-bindings-smoke"));
    child_command.AppendSwitch("child");
    base::LaunchOptions options;
    channel->PrepareToPassRemoteEndpoint(&options, &child_command);
    process = base::LaunchProcess(child_command, options);
    channel->RemoteProcessLaunchAttempted();
    CHECK(process.IsValid());
  }
  base::SingleThreadTaskExecutor executor;
  mojo::core::Configuration configuration;
  configuration.is_broker_process = !child;
  configuration.force_direct_shared_memory_allocation = true;
  configuration.max_shared_memory_num_bytes = 16 * 1024 * 1024;
  mojo::core::Init(configuration);
  CHECK(mojo::core::IsMojoIpczEnabled());
  base::Thread io("ALOSBindingsIO");
  base::Thread::Options io_options;
  io_options.message_pump_type = base::MessagePumpType::IO;
  CHECK(io.StartWithOptions(std::move(io_options)));
  {
    mojo::core::ScopedIPCSupport support(
        io.task_runner(), mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
    if (child) {
      auto invitation = mojo::IncomingInvitation::Accept(
          mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(command));
      EchoService service;
      mojo::Receiver<alos::mojom::Echo> receiver(
          &service, mojo::PendingReceiver<alos::mojom::Echo>(
                        invitation.ExtractMessagePipe("echo")));
      base::RunLoop loop;
      receiver.set_disconnect_handler(loop.QuitClosure());
      loop.Run();
      CHECK(service.finished);
      CHECK_EQ(service.exchanges, 1U);
      puts("chromium-mojo-bindings-smoke: child typed request PASS");
    } else {
      mojo::OutgoingInvitation invitation;
      mojo::Remote<alos::mojom::Echo> remote(
          mojo::PendingRemote<alos::mojom::Echo>(
              invitation.AttachMessagePipe("echo"), 0));
      mojo::OutgoingInvitation::Send(std::move(invitation), process.Handle(),
                                      channel->TakeLocalEndpoint());
      auto buffer = mojo::SharedBufferHandle::Create(4096);
      CHECK(buffer.is_valid());
      auto mapping = buffer->Map(4096);
      CHECK(mapping);
      auto* bytes = static_cast<unsigned char*>(mapping.get());
      bytes[19] = 73;
      base::RunLoop exchange;
      remote.set_disconnect_handler(base::BindOnce([] {
        CHECK(false) << "Typed Mojo peer disconnected before response";
      }));
      bool response = false;
      remote->Exchange("typed request", std::move(buffer),
          base::BindOnce([](unsigned char* shared, bool* received,
                            base::OnceClosure quit,
                            const std::string& reply, uint8_t value) {
            CHECK_EQ(reply, "typed response");
            CHECK_EQ(value, 91);
            CHECK_EQ(shared[19], 91);
            *received = true;
            std::move(quit).Run();
          }, bytes, &response, exchange.QuitClosure()));
      exchange.Run();
      CHECK(response);
      base::RunLoop finish;
      remote->Finish(finish.QuitClosure());
      finish.Run();
      remote.reset();
      int status;
      CHECK(process.WaitForExitWithTimeout(base::Seconds(15), &status));
      CHECK_EQ(status, 0);
    }
  }
  mojo::core::ShutDown();
  io.Stop();
  if (!child) puts("chromium-mojo-bindings-smoke: PASS (generated C++, default ipcz)");
  return 0;
}
