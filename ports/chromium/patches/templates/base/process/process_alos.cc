// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "base/process/process.h"

#include <errno.h>
#include <sys/alos_process_control.h>
#include <utility>
#include <algorithm>

#include "base/logging.h"
#include "base/check.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"

namespace base {

Process::Process(ProcessHandle handle) : process_(handle) {}
Process::Process(Process&& other) : process_(other.Release()) {}
Process& Process::operator=(Process&& other) {
  if (this != &other) process_ = other.Release();
  return *this;
}
Process::~Process() = default;
Process Process::Current() { return Process(GetCurrentProcessHandle()); }
Process Process::Open(ProcessId pid) {
  alos_process_info_t info{};
  if (alos_process_query(pid, &info) != 0) {
    PLOG(ERROR) << "ALOS process is absent or outside native access scope";
    return Process();
  }
  return Process(pid);
}
Process Process::OpenWithExtraPrivileges(ProcessId pid) { return Open(pid); }
void Process::TerminateCurrentProcessImmediately(int exit_code) {
  alos_process_exit(exit_code);
}
bool Process::IsValid() const { return process_ > 0; }
ProcessHandle Process::Handle() const { return process_; }
Process Process::Duplicate() const { return Process(process_); }
ProcessHandle Process::Release() {
  return std::exchange(process_, kNullProcessHandle);
}
ProcessId Process::Pid() const {
  DCHECK(IsValid());
  return process_;
}
bool Process::is_current() const {
  return process_ == GetCurrentProcessHandle();
}
void Process::Close() { process_ = kNullProcessHandle; }
bool Process::Terminate(int exit_code, bool wait) const {
  DCHECK(IsValid());
  CHECK_GT(process_, 0);
  return TerminateInternal(exit_code, wait);
}
bool Process::TerminateInternal(int exit_code, bool wait) const {
  if (!IsValid()) {
    errno = EINVAL;
    PLOG(ERROR) << "ALOS cannot terminate an invalid process";
    return false;
  }
  if (is_current()) alos_process_exit(exit_code);
  if (alos_process_terminate(process_, exit_code) != 0) {
    PLOG(ERROR) << "ALOS native process termination failed";
    return false;
  }
  return !wait || WaitForExit(nullptr);
}
bool Process::WaitForExit(int* exit_code) const {
  return WaitForExitWithTimeout(TimeDelta::Max(), exit_code);
}
bool Process::WaitForExitWithTimeout(TimeDelta timeout, int* exit_code) const {
  if (!timeout.is_zero()) internal::AssertBaseSyncPrimitivesAllowed();
  return WaitForExitWithTimeoutImpl(process_, exit_code,
                                    std::max(timeout, TimeDelta()));
}
bool Process::WaitForExitWithTimeoutImpl(ProcessHandle handle, int* exit_code,
                                        TimeDelta timeout) const {
  uint32_t native_timeout;
  if (timeout.is_max()) native_timeout = ALOS_PROCESS_WAIT_FOREVER;
  else {
    const int64_t milliseconds = timeout.InMillisecondsRoundedUp();
    if (milliseconds < 0 ||
        milliseconds >= static_cast<int64_t>(ALOS_PROCESS_WAIT_FOREVER)) {
      errno = EOVERFLOW;
      PLOG(ERROR) << "ALOS process wait exceeds the finite timeout range";
      return false;
    }
    native_timeout = static_cast<uint32_t>(milliseconds);
  }
  alos_process_exit_t result;
  const int pid = alos_process_wait(handle, &result, native_timeout);
  if (pid < 0) {
    PLOG(ERROR) << "ALOS wait requires an unreaped direct child";
    return false;
  }
  if (!pid) return false;
  if (exit_code) *exit_code = result.raw_status;
  return true;
}
void Process::Exited(int exit_code) const {
  // Native wait already reaps the child; PID values own no extra OS handle.
  (void)exit_code;
}
int Process::GetOSPriority() const {
  DCHECK(IsValid());
  alos_process_info_t info{};
  if (alos_process_query(process_, &info) != 0) {
    PLOG(ERROR) << "ALOS cannot query this process's main-thread nice value";
    return -1;
  }
  if (!info.has_main_thread_nice) {
    errno = ESRCH;
    PLOG(ERROR) << "ALOS process has no live main-thread priority";
    return -1;
  }
  return info.main_thread_nice;
}
bool Process::CanSetPriority() { return false; }
Process::Priority Process::GetPriority() const {
  DCHECK(IsValid());
  alos_process_info_t info{};
  if (alos_process_query(process_, &info) != 0 ||
      !info.has_main_thread_nice) {
    PLOG(ERROR) << "ALOS process priority is unavailable";
    return Priority::kUserBlocking;
  }
  if (info.main_thread_nice >= 19) return Priority::kBestEffort;
  if (info.main_thread_nice > 0) return Priority::kUserVisible;
  return Priority::kUserBlocking;
}
bool Process::SetPriority(Priority priority) {
  (void)priority;
  errno = ENOTSUP;
  PLOG(ERROR) << "ALOS does not provide process-wide priority changes";
  return false;
}

}  // namespace base
