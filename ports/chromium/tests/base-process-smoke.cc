#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/process/kill.h"
#include "base/process/launch.h"
#include "base/time/time.h"

#include <errno.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <unistd.h>

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::CommandLine::Init(argc, argv);
  if (argc == 2 && std::string(argv[1]) == "--raw-37") return 37;
  if (argc == 2 && std::string(argv[1]) == "--output") {
    puts("native-base-output");
    return 9;
  }
  if (argc == 2 && std::string(argv[1]) == "--sleep") {
    usleep(5000000);
    return 0;
  }
  const std::string executable = "/bin/chromium-process-smoke";
  base::Process child = base::LaunchProcess(
      std::vector<std::string>{executable, "--raw-37"}, {});
  int raw = -1;
  if (!child.IsValid() || !child.WaitForExit(&raw) || raw != 37) {
    puts("chromium-process-smoke: FAIL raw child wait");
    return 1;
  }
  std::string output;
  if (!base::GetAppOutputWithExitCode(
          std::vector<std::string>{executable, "--output"}, &output, &raw) ||
      output != "native-base-output\n" || raw != 9) {
    puts("chromium-process-smoke: FAIL captured native output");
    return 1;
  }
  child = base::LaunchProcess(std::vector<std::string>{executable, "--sleep"}, {});
  if (!child.IsValid() ||
      child.WaitForExitWithTimeout(base::Milliseconds(20), &raw) ||
      !child.Terminate(42, false) ||
      base::GetKnownDeadTerminationStatus(child.Handle(), &raw) !=
          base::TERMINATION_STATUS_PROCESS_WAS_KILLED || raw != 42) {
    puts("chromium-process-smoke: FAIL native force/timeout");
    return 1;
  }
  base::LaunchOptions unsupported;
  unsupported.new_process_group = true;
  errno = 0;
  if (base::LaunchProcess(std::vector<std::string>{executable},
                          unsupported).IsValid() || errno != ENOTSUP) {
    puts("chromium-process-smoke: FAIL unavailable options");
    return 1;
  }
  puts("chromium-process-smoke: PASS (native raw ABI, no POSIX signals)");
  return 0;
}
