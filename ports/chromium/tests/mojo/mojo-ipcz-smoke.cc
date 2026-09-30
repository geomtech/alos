#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/process/launch.h"
#include "base/threading/thread.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/buffer.h"
#include "mojo/public/cpp/system/invitation.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/wait.h"
#include <stdio.h>
#include <string.h>
#include <optional>
#include <vector>

int main(int argc, char **argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  const bool child = command_line.HasSwitch("child");
  std::optional<mojo::PlatformChannel> channel;
  base::Process process;
  // Fork/exec avant toute creation de thread, comme l'exige ALOS.
  if (!child) {
    channel.emplace();
    base::CommandLine child_command(base::FilePath("/bin/chromium-mojo-smoke"));
    child_command.AppendSwitch("child");
    base::LaunchOptions launch;
    channel->PrepareToPassRemoteEndpoint(&launch, &child_command);
    process = base::LaunchProcess(child_command, launch);
    channel->RemoteProcessLaunchAttempted();
    CHECK(process.IsValid());
  }
  mojo::core::Configuration configuration;
  configuration.is_broker_process = !child;
  configuration.force_direct_shared_memory_allocation = true;
  configuration.max_shared_memory_num_bytes = 16 * 1024 * 1024;
  mojo::core::Init(configuration);
  CHECK(mojo::core::IsMojoIpczEnabled());
  base::Thread io_thread("ALOSMojoIO");
  base::Thread::Options io_options;
  io_options.message_pump_type = base::MessagePumpType::IO;
  CHECK(io_thread.StartWithOptions(std::move(io_options)));
  {
    mojo::core::ScopedIPCSupport support(
        io_thread.task_runner(), mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
    mojo::ScopedMessagePipeHandle pipe;
    if (child) {
      auto invitation = mojo::IncomingInvitation::Accept(
          mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(command_line));
      pipe = invitation.ExtractMessagePipe("alos");
    } else {
      mojo::OutgoingInvitation invitation;
      pipe = invitation.AttachMessagePipe("alos");
      mojo::OutgoingInvitation::Send(std::move(invitation), process.Handle(),
                                      channel->TakeLocalEndpoint());
    }
    CHECK(pipe.is_valid());
    puts(child ? "chromium-mojo-smoke: child invitation ready" :
                 "chromium-mojo-smoke: parent invitation sent");
    if (child) {
      CHECK_EQ(mojo::Wait(pipe.get(), MOJO_HANDLE_SIGNAL_READABLE), MOJO_RESULT_OK);
      std::vector<uint8_t> payload;
      std::vector<mojo::ScopedHandle> handles;
      CHECK_EQ(mojo::ReadMessageRaw(pipe.get(), &payload, &handles,
                                    MOJO_READ_MESSAGE_FLAG_NONE), MOJO_RESULT_OK);
      CHECK_EQ(payload.size(), 5U);
      CHECK_EQ(memcmp(payload.data(), "share", 5), 0);
      CHECK_EQ(handles.size(), 1U);
      puts("chromium-mojo-smoke: child message/handle received");
      mojo::ScopedSharedBufferHandle buffer(
          mojo::SharedBufferHandle(handles[0].release().value()));
      CHECK_EQ(buffer->GetSize(), 4096U);
      auto mapping = buffer->Map(4096);
      CHECK(mapping);
      auto* bytes = static_cast<unsigned char*>(mapping.get());
      CHECK_EQ(bytes[19], 73);
      bytes[19] = 91;
      CHECK_EQ(mojo::WriteMessageRaw(pipe.get(), "ack", 3, nullptr, 0,
                                     MOJO_WRITE_MESSAGE_FLAG_NONE), MOJO_RESULT_OK);
      puts("chromium-mojo-smoke: child acknowledgement submitted");
      // WriteMessage est asynchrone : attendre la confirmation distante avant
      // de detruire le noeud et son transport.
      CHECK_EQ(mojo::Wait(pipe.get(), MOJO_HANDLE_SIGNAL_READABLE), MOJO_RESULT_OK);
      payload.clear();
      handles.clear();
      CHECK_EQ(mojo::ReadMessageRaw(pipe.get(), &payload, &handles,
                                    MOJO_READ_MESSAGE_FLAG_NONE), MOJO_RESULT_OK);
      CHECK_EQ(payload.size(), 4U);
      CHECK_EQ(memcmp(payload.data(), "done", 4), 0);
      CHECK(handles.empty());
      puts("chromium-mojo-smoke: child shared-handle PASS");
    } else {
      auto buffer = mojo::SharedBufferHandle::Create(4096);
      CHECK(buffer.is_valid());
      auto mapping = buffer->Map(4096);
      CHECK(mapping);
      auto* bytes = static_cast<unsigned char*>(mapping.get());
      bytes[19] = 73;
      MojoHandle handle = buffer.release().value();
      CHECK_EQ(mojo::WriteMessageRaw(pipe.get(), "share", 5, &handle, 1,
                                     MOJO_WRITE_MESSAGE_FLAG_NONE), MOJO_RESULT_OK);
      puts("chromium-mojo-smoke: parent shared-handle submitted");
      CHECK_EQ(mojo::Wait(pipe.get(), MOJO_HANDLE_SIGNAL_READABLE), MOJO_RESULT_OK);
      std::vector<uint8_t> payload;
      std::vector<mojo::ScopedHandle> handles;
      CHECK_EQ(mojo::ReadMessageRaw(pipe.get(), &payload, &handles,
                                    MOJO_READ_MESSAGE_FLAG_NONE), MOJO_RESULT_OK);
      CHECK_EQ(payload.size(), 3U);
      CHECK_EQ(memcmp(payload.data(), "ack", 3), 0);
      CHECK(handles.empty());
      CHECK_EQ(bytes[19], 91);
      puts("chromium-mojo-smoke: parent acknowledgement received");
      CHECK_EQ(mojo::WriteMessageRaw(pipe.get(), "done", 4, nullptr, 0,
                                     MOJO_WRITE_MESSAGE_FLAG_NONE), MOJO_RESULT_OK);
      CHECK_EQ(mojo::Wait(pipe.get(), MOJO_HANDLE_SIGNAL_PEER_CLOSED), MOJO_RESULT_OK);
      int status;
      CHECK(process.WaitForExitWithTimeout(base::Seconds(15), &status));
      CHECK_EQ(status, 0);
    }
  }
  mojo::core::ShutDown();
  io_thread.Stop();
  if (!child) puts("chromium-mojo-smoke: PASS (default ipcz, parent/child, shared handle)");
  return 0;
}
