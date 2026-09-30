// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "base/process/kill.h"

#include <errno.h>
#include <sys/alos_process_control.h>
#include <utility>

#include "base/check.h"
#include "base/logging.h"
#include "base/threading/platform_thread.h"

namespace base {
namespace {
TerminationStatus NativeStatus(ProcessHandle handle, uint32_t timeout,
                                int* exit_code) {
  CHECK(exit_code);
  alos_process_exit_t native{};
  int pid = alos_process_wait(handle, &native, timeout);
  if (pid < 0) {
    PLOG(ERROR) << "ALOS cannot inspect or reap this native child";
    *exit_code = -1;
    return TERMINATION_STATUS_ABNORMAL_TERMINATION;
  }
  if (!pid) {
    *exit_code = 0;
    return TERMINATION_STATUS_STILL_RUNNING;
  }
  *exit_code = native.raw_status;
  if (native.reason == ALOS_PROCESS_EXIT_FORCED)
    return TERMINATION_STATUS_PROCESS_WAS_KILLED;
  return native.raw_status == 0 ? TERMINATION_STATUS_NORMAL_TERMINATION
                                : TERMINATION_STATUS_ABNORMAL_TERMINATION;
}
class NativeReaper : public PlatformThread::Delegate {
 public:
  explicit NativeReaper(Process process) : process_(std::move(process)) {}
  void ThreadMain() override {
    process_.WaitForExit(nullptr);
    delete this;
  }
 private:
  Process process_;
};
}  // namespace

TerminationStatus GetTerminationStatus(ProcessHandle handle, int* exit_code) {
  return NativeStatus(handle, 0, exit_code);
}
TerminationStatus GetKnownDeadTerminationStatus(ProcessHandle handle,
                                                int* exit_code) {
  if (alos_process_terminate(handle, -1) != 0) {
    PLOG(ERROR) << "ALOS cannot request native child termination";
    *exit_code = -1;
    return TERMINATION_STATUS_ABNORMAL_TERMINATION;
  }
  return NativeStatus(handle, ALOS_PROCESS_WAIT_FOREVER, exit_code);
}
bool WaitForProcessesToExit(const FilePath::StringType& executable_name,
                            TimeDelta wait, const ProcessFilter* filter) {
  (void)executable_name;
  (void)wait;
  (void)filter;
  errno = ENOTSUP;
  PLOG(ERROR) << "ALOS has no name-based process enumeration contract";
  return false;
}
bool CleanupProcesses(const FilePath::StringType& executable_name,
                       TimeDelta wait, int exit_code,
                       const ProcessFilter* filter) {
  (void)executable_name;
  (void)wait;
  (void)exit_code;
  (void)filter;
  errno = ENOTSUP;
  PLOG(ERROR) << "ALOS cleanup requires explicit native child PID handles";
  return false;
}
void EnsureProcessTerminated(Process process) {
  if (!process.Terminate(-1, false)) return;
  auto* reaper = new NativeReaper(std::move(process));
  if (!PlatformThread::CreateNonJoinable(0, reaper)) {
    delete reaper;
    LOG(ERROR) << "ALOS could not create a native child reaper";
  }
}
}  // namespace base
