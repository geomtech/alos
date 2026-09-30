// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "base/process/launch.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/alos_process_control.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdlib.h>
#include <climits>

#include <algorithm>
#include <set>
#include <vector>

#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/check.h"
#include "base/posix/file_descriptor_shuffle.h"
#include "base/threading/thread_restrictions.h"

namespace base {
namespace {
bool CloseDescriptorTable(const InjectiveMultimap& saved_map) {
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return false;
  if (limit.rlim_cur > INT_MAX) { errno = EOVERFLOW; return false; }
  for (int fd = 3; fd < static_cast<int>(limit.rlim_cur); ++fd) {
    bool keep = false;
    for (const auto& arc : saved_map) if (arc.dest == fd) keep = true;
    if (!keep && close(fd) != 0 && errno != EBADF) return false;
  }
  return true;
}
[[noreturn]] void ChildFailure(int writer, int error) {
  if (!error) error = EIO;  // Legacy exec/chdir report a raw failure.
  write(writer, &error, sizeof(error));
  alos_process_exit(127);
}

bool CaptureOutput(const std::vector<std::string>& argv, bool include_error,
                    std::string* output, int* exit_code) {
  CHECK(output);
  CHECK(exit_code);
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) {
    PLOG(ERROR) << "ALOS output pipe creation failed";
    return false;
  }
  ScopedFD reader(fds[0]), writer(fds[1]);
  LaunchOptions options;
  options.fds_to_remap.emplace_back(writer.get(), STDOUT_FILENO);
  if (include_error) options.fds_to_remap.emplace_back(writer.get(), STDERR_FILENO);
  Process process = LaunchProcess(argv, options);
  writer.reset();
  if (!process.IsValid()) return false;
  output->clear();
  bool success = true;
  for (;;) {
    char bytes[4096];
    ssize_t count = read(reader.get(), bytes, sizeof(bytes));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      PLOG(ERROR) << "ALOS process output read failed";
      success = false;
      process.Terminate(127, false);
      break;
    }
    if (!count) break;
    output->append(bytes, static_cast<size_t>(count));
  }
  reader.reset();
  internal::GetAppOutputScopedAllowBaseSyncPrimitives allow_wait;
  return process.WaitForExit(exit_code) && success;
}
}  // namespace

void CloseSuperfluousFds(const InjectiveMultimap& saved_map) {
  PCHECK(CloseDescriptorTable(saved_map));
}

Process LaunchProcess(const CommandLine& command_line,
                       const LaunchOptions& options) {
  return LaunchProcess(command_line.argv(), options);
}

Process LaunchProcess(const std::vector<std::string>& argv,
                       const LaunchOptions& options) {
  if (argv.empty() || argv.size() > 64 ||
      options.new_process_group || options.pre_exec_delegate ||
      !options.environment.empty() ||
      (options.maximize_rlimits && !options.maximize_rlimits->empty())) {
    errno = argv.empty() ? EINVAL : ENOTSUP;
    if (argv.size() > 64) errno = E2BIG;
    PLOG(ERROR) << "ALOS launch options exceed the native static-ELF backend";
    return Process();
  }
  const FilePath program = options.real_path.empty() ? FilePath(argv[0])
                                                    : options.real_path;
  if (!program.IsAbsolute()) {
    errno = ENOTSUP;
    PLOG(ERROR) << "ALOS launch requires an absolute executable path";
    return Process();
  }
  alos_process_info_t self{};
  if (alos_process_query(GetCurrentProcId(), &self) != 0) {
    PLOG(ERROR) << "ALOS cannot inspect the launching process";
    return Process();
  }
  if (self.thread_count != 1) {
    errno = ENOTSUP;
    PLOG(ERROR) << "ALOS fork/exec launch requires a single-threaded parent";
    return Process();
  }
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur > INT_MAX) {
    PLOG(ERROR) << "ALOS cannot determine its descriptor table capacity";
    return Process();
  }
  InjectiveMultimap mapping;
  std::set<int> destinations;
  for (const auto& [source, destination] : options.fds_to_remap) {
    if (source < 0 || destination < 0 ||
        static_cast<uint64_t>(destination) >= limit.rlim_cur ||
        !destinations.insert(destination).second || fcntl(source, F_GETFD) < 0) {
      errno = EINVAL;
      PLOG(ERROR) << "Invalid ALOS descriptor remapping";
      return Process();
    }
    mapping.emplace_back(source, destination, true);
  }
  std::vector<char*> arguments;
  for (const std::string& value : argv) {
    if (value.find('\0') != std::string::npos) {
      errno = EINVAL;
      PLOG(ERROR) << "ALOS executable arguments cannot contain embedded NUL";
      return Process();
    }
    arguments.push_back(const_cast<char*>(value.c_str()));
  }
  arguments.push_back(nullptr);
  int fds[2];
  if (pipe2(fds, O_CLOEXEC) != 0) {
    PLOG(ERROR) << "ALOS exec-result pipe creation failed";
    return Process();
  }
  ScopedFD reader(fds[0]), writer(fds[1]);
  if (destinations.contains(writer.get())) {
    std::vector<ScopedFD> collisions;
    for (;;) {
      int candidate = fcntl(writer.get(), F_DUPFD_CLOEXEC, 3);
      if (candidate < 0) {
        PLOG(ERROR) << "ALOS cannot reserve an exec-result descriptor";
        return Process();
      }
      if (!destinations.contains(candidate)) {
        writer.reset(candidate);
        break;
      }
      collisions.emplace_back(candidate);
    }
  }
  const int error_writer = writer.get();
  mapping.emplace_back(error_writer, error_writer, false);
  const InjectiveMultimap saved_mapping = mapping;
  const int child = fork();
  if (child < 0) {
    PLOG(ERROR) << "ALOS native fork failed";
    return Process();
  }
  if (!child) {
    reader.reset();
    if (!ShuffleFileDescriptors(&mapping)) ChildFailure(error_writer, errno);
    for (const auto& [source, destination] : options.fds_to_remap) {
      (void)source;
      if (fcntl(destination, F_SETFD, 0) != 0) ChildFailure(error_writer, errno);
    }
    if (!CloseDescriptorTable(saved_mapping)) ChildFailure(error_writer, errno);
    if (!options.current_directory.empty() &&
        chdir(options.current_directory.value().c_str()) != 0)
      ChildFailure(error_writer, errno);
    char* empty_environment[] = {nullptr};
    errno = 0;
    execve(program.value().c_str(), arguments.data(),
            options.clear_environment ? empty_environment : environ);
    ChildFailure(error_writer, errno);
  }
  writer.reset();
  int error = 0;
  size_t received = 0;
  while (received < sizeof(error)) {
    ssize_t count = read(reader.get(), reinterpret_cast<char*>(&error) + received,
                          sizeof(error) - received);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { error = errno; break; }
    if (!count) {
      if (received) error = EIO;
      break;
    }
    received += static_cast<size_t>(count);
  }
  reader.reset();
  Process process(child);
  if (error) {
    process.Terminate(127, true);
    errno = error;
    PLOG(ERROR) << "ALOS child failed before or during static ELF exec";
    return Process();
  }
  if (options.wait && !process.WaitForExit(nullptr)) return Process();
  return process;
}

void RaiseProcessToHighPriority() {
  errno = ENOTSUP;
  PLOG(ERROR) << "ALOS does not expose process-wide priority changes";
}
bool GetAppOutput(const CommandLine& command_line, std::string* output) {
  return GetAppOutput(command_line.argv(), output);
}
bool GetAppOutput(const std::vector<std::string>& argv, std::string* output) {
  int status = -1;
  return CaptureOutput(argv, false, output, &status) && status == 0;
}
bool GetAppOutputAndError(const CommandLine& command_line, std::string* output) {
  return GetAppOutputAndError(command_line.argv(), output);
}
bool GetAppOutputAndError(const std::vector<std::string>& argv,
                          std::string* output) {
  int status = -1;
  return CaptureOutput(argv, true, output, &status) && status == 0;
}
bool GetAppOutputWithExitCode(const CommandLine& command_line,
                             std::string* output, int* exit_code) {
  return CaptureOutput(command_line.argv(), false, output, exit_code);
}
bool GetAppOutputWithExitCode(const std::vector<std::string>& argv,
                             std::string* output, int* exit_code) {
  return CaptureOutput(argv, false, output, exit_code);
}
}  // namespace base
