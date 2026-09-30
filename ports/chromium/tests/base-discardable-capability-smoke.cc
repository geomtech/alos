#include "base/at_exit.h"
#include "base/memory/discardable_shared_memory.h"
#include "base/time/time.h"
#include <errno.h>
#include <stdio.h>

int main() {
  base::AtExitManager at_exit;
  base::DiscardableSharedMemory memory;
  if (!memory.CreateAndMap(4096)) {
    puts("chromium-discardable-smoke: FAIL allocation");
    return 1;
  }
  memory.memory()[0] = 73;
  memory.Unlock(0, 0);
  errno = 0;
  if (memory.Purge(base::Time::Now()) || errno != ENOTSUP ||
      !memory.IsMemoryResident() ||
      memory.Lock(0, 0) != base::DiscardableSharedMemory::SUCCESS ||
      memory.memory()[0] != 73) {
    puts("chromium-discardable-smoke: FAIL unavailable purge changed state");
    return 1;
  }
  memory.Unlock(0, 0);
  puts("chromium-discardable-smoke: PASS (purge explicitly unavailable)");
  return 0;
}
