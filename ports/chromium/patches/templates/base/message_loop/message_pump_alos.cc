// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "base/message_loop/message_pump_alos.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/logging.h"
#include "base/time/time.h"

namespace base {
namespace {
constexpr size_t kMaxWatchedDescriptors = 1023;  // One poll slot is the wake pipe.
short PollEvents(int mode) {
  return static_cast<short>(((mode & MessagePumpAlos::WATCH_READ) ? POLLIN : 0) |
                            ((mode & MessagePumpAlos::WATCH_WRITE) ? POLLOUT : 0));
}
}  // namespace

struct MessagePumpAlos::Registration {
  int fd = -1;
  int mode = 0;
  bool persistent = false;
  bool active = false;
  uint64_t generation = 0;
  WeakPtr<FdWatchController> controller;
};

struct MessagePumpAlos::ReadyEvent {
  std::shared_ptr<Registration> registration;
  uint64_t generation;
  short revents;
};

MessagePumpAlos::FdWatchController::FdWatchController(const Location& from_here)
    : FdWatchControllerInterface(from_here) {}

MessagePumpAlos::FdWatchController::~FdWatchController() {
  CHECK(StopWatchingFileDescriptor());
}

bool MessagePumpAlos::FdWatchController::StopWatchingFileDescriptor() {
  weak_factory_.InvalidateWeakPtrs();
  if (registration_) {
    registration_->active = false;
    ++registration_->generation;
    if (pump_) {
      pump_->RemoveRegistration(registration_);
    }
    registration_.reset();
  }
  watcher_ = nullptr;
  pump_.reset();
  return true;
}

MessagePumpAlos::MessagePumpAlos() {
  int descriptors[2];
  const int result = pipe2(descriptors, O_NONBLOCK | O_CLOEXEC);
  PCHECK(result == 0) << "ALOS message pump requires a native wake pipe";
  wake_read_.reset(descriptors[0]);
  wake_write_.reset(descriptors[1]);
}

MessagePumpAlos::~MessagePumpAlos() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  weak_factory_.InvalidateWeakPtrs();
  for (auto& [fd, interests] : registrations_) {
    for (auto& interest : interests) {
      interest->active = false;
      ++interest->generation;
    }
  }
}

bool MessagePumpAlos::WatchFileDescriptor(int fd, bool persistent, int mode,
                                         FdWatchController* controller,
                                         FdWatcher* watcher) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  CHECK(controller);
  CHECK(watcher);
  auto reject = [&](int error) {
    controller->StopWatchingFileDescriptor();
    errno = error;
    PLOG(ERROR) << "ALOS cannot watch descriptor " << fd;
    return false;
  };
  if (fd < 0 || mode < WATCH_READ || mode > WATCH_READ_WRITE) {
    return reject(EINVAL);
  }
  if (controller->registration_) {
    if (controller->registration_->fd != fd ||
        (controller->pump_ && controller->pump_.get() != this)) {
      return reject(EINVAL);
    }
    mode |= controller->registration_->mode;
  }
  if (!registrations_.contains(fd) &&
      registrations_.size() >= kMaxWatchedDescriptors) {
    return reject(ENOSPC);
  }
  struct pollfd probe { fd, PollEvents(mode), 0 };
  int result;
  do {
    result = poll(&probe, 1, 0);
  } while (result < 0 && errno == EINTR);
  if (result < 0) {
    return reject(errno);
  }
  if (probe.revents & POLLNVAL) {
    return reject(EBADF);
  }

  controller->StopWatchingFileDescriptor();
  auto interest = std::make_shared<Registration>();
  interest->fd = fd;
  interest->mode = mode;
  interest->persistent = persistent;
  interest->active = true;
  interest->generation = 1;
  interest->controller = controller->weak_factory_.GetWeakPtr();
  controller->registration_ = interest;
  controller->watcher_ = watcher;
  controller->pump_ = weak_factory_.GetWeakPtr();
  registrations_[fd].push_back(std::move(interest));
  return true;
}

void MessagePumpAlos::RemoveRegistration(
    const std::shared_ptr<Registration>& interest) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto entry = registrations_.find(interest->fd);
  if (entry == registrations_.end()) {
    return;
  }
  auto& list = entry->second;
  std::erase(list, interest);
  if (list.empty()) {
    registrations_.erase(entry);
  }
}

void MessagePumpAlos::Run(Delegate* delegate) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  RunState state(delegate);
  AutoReset<raw_ptr<RunState>> restore(&run_state_, &state);
  for (;;) {
    const Delegate::NextWorkInfo next = delegate->DoWork();
    if (state.should_quit) {
      break;
    }
    state.native_work_started = false;
    const bool did_io = WaitForIOEvents(TimeTicks(), false);
    if (state.should_quit) {
      break;
    }
    if (next.is_immediate() || did_io) {
      continue;
    }
    delegate->DoIdleWork();
    if (state.should_quit) {
      break;
    }
    delegate->BeforeWait();
    if (state.should_quit) {
      break;
    }
    WaitForIOEvents(next.delayed_run_time, true);
    if (state.should_quit) {
      break;
    }
  }
}

void MessagePumpAlos::Quit() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  CHECK(run_state_);
  run_state_->should_quit = true;
}

void MessagePumpAlos::ScheduleWork() {
  const unsigned char byte = 1;
  ssize_t result;
  do {
    result = write(wake_write_.get(), &byte, sizeof(byte));
  } while (result < 0 && errno == EINTR);
  PCHECK(result == 1 || (result < 0 && errno == EAGAIN))
      << "ALOS wake pipe write failed";
}

void MessagePumpAlos::ScheduleDelayedWork(
    const Delegate::NextWorkInfo& next_work_info) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Re-enter DoWork rather than sleeping on a deadline superseded by a callout.
  (void)next_work_info;
  ScheduleWork();
}

bool MessagePumpAlos::WaitForIOEvents(TimeTicks deadline, bool blocking) {
  std::vector<struct pollfd> descriptors;
  std::vector<std::vector<ReadyEvent>> snapshots;
  descriptors.push_back({wake_read_.get(), POLLIN, 0});
  for (const auto& [fd, interests] : registrations_) {
    short events = 0;
    std::vector<ReadyEvent> snapshot;
    for (const auto& interest : interests) {
      if (interest->active) {
        events |= PollEvents(interest->mode);
        snapshot.push_back({interest, interest->generation, 0});
      }
    }
    if (!snapshot.empty()) {
      descriptors.push_back({fd, events, 0});
      snapshots.push_back(std::move(snapshot));
    }
  }
  CHECK_LE(descriptors.size(), kMaxWatchedDescriptors + 1);
  int result;
  for (;;) {
    int timeout = 0;
    if (blocking) {
      CHECK(!deadline.is_null());
      if (deadline.is_max()) {
        timeout = -1;
      } else {
        const TimeDelta remaining = deadline - TimeTicks::Now();
        const int64_t ms = remaining.InMillisecondsRoundedUp();
        timeout = static_cast<int>(std::clamp<int64_t>(ms, 0, INT_MAX));
      }
    }
    result = poll(descriptors.data(), descriptors.size(), timeout);
    if (result >= 0 || errno != EINTR) {
      break;
    }
  }
  PCHECK(result >= 0) << "Native ALOS poll failed";
  if (!result) {
    return false;
  }
  if (run_state_ && !run_state_->native_work_started) {
    run_state_->delegate->BeginNativeWorkBeforeDoWork();
    run_state_->native_work_started = true;
    if (run_state_->should_quit) {
      return true;
    }
  }
  if (descriptors[0].revents) {
    CHECK(!(descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)));
    DrainWakeup();
  }
  std::vector<ReadyEvent> ready;
  for (size_t i = 1; i < descriptors.size(); ++i) {
    if (!descriptors[i].revents) {
      continue;
    }
    for (auto event : snapshots[i - 1]) {
      event.revents = descriptors[i].revents;
      ready.push_back(std::move(event));
    }
  }
  DispatchReady(ready);
  return true;
}

void MessagePumpAlos::DispatchReady(const std::vector<ReadyEvent>& events) {
  for (const auto& event : events) {
    if (run_state_ && run_state_->should_quit) {
      return;
    }
    const auto& interest = event.registration;
    if (!interest->active || interest->generation != event.generation ||
        !interest->controller) {
      continue;
    }
    WeakPtr<FdWatchController> controller = interest->controller;
    if (event.revents & POLLNVAL) {
      LOG(ERROR) << "ALOS watch descriptor closed without stopping: " << interest->fd;
      controller->StopWatchingFileDescriptor();
      continue;
    }
    const bool disconnected = event.revents & (POLLHUP | POLLERR);
    const bool readable = (event.revents & POLLIN) || disconnected;
    const bool writable = (event.revents & POLLOUT) || disconnected;
    const bool read = readable && (interest->mode & WATCH_READ);
    const bool write = writable && (interest->mode & WATCH_WRITE);
    if (!read && !write) {
      continue;
    }
    const bool persistent = interest->persistent;
    if (!persistent) {
      interest->active = false;
      ++interest->generation;
      RemoveRegistration(interest);
    }
    const uint64_t dispatch_generation = interest->generation;
    Delegate::ScopedDoWorkItem work;
    if (run_state_) {
      work = run_state_->delegate->BeginWorkItem();
    }
    if ((run_state_ && run_state_->should_quit) || !controller ||
        interest->generation != dispatch_generation ||
        controller->registration_ != interest) {
      continue;
    }
    if (write && (persistent || !read)) {
      controller->watcher_->OnFileCanWriteWithoutBlocking(interest->fd);
      if ((run_state_ && run_state_->should_quit) || !controller) {
        if (run_state_ && run_state_->should_quit) {
          return;
        }
        continue;
      }
      if (persistent &&
          (!interest->active || interest->generation != event.generation)) {
        continue;
      }
    }
    if (read && controller) {
      controller->watcher_->OnFileCanReadWithoutBlocking(interest->fd);
      if (run_state_ && run_state_->should_quit) {
        return;
      }
    }
  }
}

void MessagePumpAlos::DrainWakeup() {
  unsigned char bytes[256];
  for (int reads = 0; reads < 16; ++reads) {
    ssize_t result;
    do {
      result = read(wake_read_.get(), bytes, sizeof(bytes));
    } while (result < 0 && errno == EINTR);
    if (result < 0 && errno == EAGAIN) {
      return;
    }
    PCHECK(result > 0) << "ALOS wake pipe read failed";
  }
}

}  // namespace base
