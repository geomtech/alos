// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "base/threading/platform_thread_internal_posix.h"

#include <errno.h>
#include <pthread.h>
#include <sys/alos_resource.h>

#include <algorithm>

#include "base/logging.h"
#include "base/notreached.h"

namespace base {

// Le noyau initialise TLS/xstate et pthread nettoie le CRT apres ThreadFunc.
void InitThreading() {}
void TerminateOnThread() {}

size_t GetDefaultThreadStackSize(const pthread_attr_t& attributes) {
  size_t size;
  CHECK_EQ(pthread_attr_getstacksize(&attributes, &size), 0);
  return std::max(size, size_t{2 * 1024 * 1024});
}

void PlatformThreadBase::SetName(const std::string& name) {
  // Metadonnees Base uniquement : aucun prctl Linux ni renommage du processus.
  SetNameCommon(name);
}

namespace internal {

const ThreadPriorityToNiceValuePairForTest kThreadPriorityToNiceValueMapForTest[7] = {
    {ThreadPriorityForTest::kInteractive, -20},
    {ThreadPriorityForTest::kInteractive, -10},
    {ThreadPriorityForTest::kDisplay, -5},
    {ThreadPriorityForTest::kNormal, 4},
    {ThreadPriorityForTest::kUtility, 14},
    {ThreadPriorityForTest::kBackground, 19},
    {ThreadPriorityForTest::kBackground, 19},
};

int ThreadTypeToNiceValue(ThreadType type) {
  // Cinq classes du scheduler ALOS, sans FIFO ni garantie audio temps reel.
  switch (type) {
    case ThreadType::kBackground: return 19;
    case ThreadType::kUtility: return 10;
    case ThreadType::kDefault: return 0;
    case ThreadType::kDisplayCritical: return -5;
    case ThreadType::kInteractive: return -10;
    case ThreadType::kRealtimeAudio: NOTREACHED();
  }
  NOTREACHED();
}

bool CanSetThreadTypeToRealtimeAudio() { return false; }

bool TrySetCurrentThreadTypeForALOS(ThreadType type, MessagePumpType) {
  if (type == ThreadType::kRealtimeAudio) {
    errno = ENOTSUP;
    PLOG(ERROR) << "ALOS has no realtime audio scheduling policy";
    return false;
  }
  if (alos_thread_set_nice(ThreadTypeToNiceValue(type)) != 0) {
    PLOG(ERROR) << "ALOS native thread priority change failed";
    return false;
  }
  return true;
}

bool SetCurrentThreadTypeForPlatform(ThreadType type, MessagePumpType hint) {
  // Le bool indique "traite", pas le succes : ne jamais essayer setpriority.
  TrySetCurrentThreadTypeForALOS(type, hint);
  return true;
}

std::optional<ThreadPriorityForTest> GetCurrentThreadPriorityForPlatformForTest() {
  int nice;
  PCHECK(alos_thread_get_nice(&nice) == 0);
  return NiceValueToThreadPriorityForTest(nice);
}

}  // namespace internal
}  // namespace base
