// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef BASE_MESSAGE_LOOP_MESSAGE_PUMP_ALOS_H_
#define BASE_MESSAGE_LOOP_MESSAGE_PUMP_ALOS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "base/base_export.h"
#include "base/files/scoped_file.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/watchable_io_message_pump_posix.h"
#include "base/threading/thread_checker.h"

namespace base {

// Native task/FD pump. This does not implement an ALOS window-system backend.
class BASE_EXPORT MessagePumpAlos : public MessagePump,
                                    public WatchableIOMessagePumpPosix {
  struct Registration;
  struct ReadyEvent;

 public:
  class BASE_EXPORT FdWatchController : public FdWatchControllerInterface {
   public:
    explicit FdWatchController(const Location& from_here);
    FdWatchController(const FdWatchController&) = delete;
    FdWatchController& operator=(const FdWatchController&) = delete;
    ~FdWatchController() override;
    bool StopWatchingFileDescriptor() override;

   private:
    friend class MessagePumpAlos;
    raw_ptr<FdWatcher> watcher_ = nullptr;
    WeakPtr<MessagePumpAlos> pump_;
    std::shared_ptr<Registration> registration_;
    WeakPtrFactory<FdWatchController> weak_factory_{this};
  };

  MessagePumpAlos();
  MessagePumpAlos(const MessagePumpAlos&) = delete;
  MessagePumpAlos& operator=(const MessagePumpAlos&) = delete;
  ~MessagePumpAlos() override;

  bool WatchFileDescriptor(int fd, bool persistent, int mode,
                           FdWatchController* controller, FdWatcher* watcher);
  void Run(Delegate* delegate) override;
  void Quit() override;
  void ScheduleWork() override;
  void ScheduleDelayedWork(
      const Delegate::NextWorkInfo& next_work_info) override;

 private:
  struct RunState {
    explicit RunState(Delegate* input) : delegate(input) {}
    raw_ptr<Delegate> delegate;
    bool should_quit = false;
    bool native_work_started = false;
  };

  void RemoveRegistration(const std::shared_ptr<Registration>& registration);
  bool WaitForIOEvents(TimeTicks deadline, bool blocking);
  void DispatchReady(const std::vector<ReadyEvent>& events);
  void DrainWakeup();

  std::map<int, std::vector<std::shared_ptr<Registration>>> registrations_;
  ScopedFD wake_read_;
  ScopedFD wake_write_;
  raw_ptr<RunState> run_state_ = nullptr;
  THREAD_CHECKER(thread_checker_);
  WeakPtrFactory<MessagePumpAlos> weak_factory_{this};
};

}  // namespace base
#endif
