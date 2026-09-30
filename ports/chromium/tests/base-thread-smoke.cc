#include "base/at_exit.h"
#include "base/threading/platform_thread.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/alos_resource.h>

class Worker : public base::PlatformThread::Delegate {
 public:
  bool passed = false;
  unsigned calls = 0;
  void ThreadMain() override {
    ++calls;
    int nice;
    pthread_attr_t attr;
    size_t size;
    if (alos_thread_get_nice(&nice) || nice != 19 ||
        base::PlatformThread::GetCurrentThreadType() != base::ThreadType::kBackground ||
        pthread_getattr_np(pthread_self(), &attr)) return;
    int error = pthread_attr_getstacksize(&attr, &size);
    pthread_attr_destroy(&attr);
    base::PlatformThread::SetName("ALOSBaseWorker");
    passed = !error && size >= 2 * 1024 * 1024 &&
             !strcmp(base::PlatformThread::GetName(), "ALOSBaseWorker");
  }
};

int main() {
  base::AtExitManager at_exit;
  using T = base::ThreadType;
  const T types[] = {T::kBackground, T::kUtility, T::kDefault,
                     T::kDisplayCritical, T::kInteractive};
  const int values[] = {19, 10, 0, -5, -10};
  for (size_t i = 0; i < 5; ++i) {
    base::PlatformThread::SetCurrentThreadType(types[i]);
    int nice;
    if (!base::PlatformThread::CanChangeThreadType(T::kDefault, types[i]) ||
        alos_thread_get_nice(&nice) || nice != values[i] ||
        base::PlatformThread::GetCurrentThreadType() != types[i]) {
      puts("chromium-thread-smoke: FAIL native priority");
      return 1;
    }
  }
  errno = 0;
  base::PlatformThread::SetCurrentThreadType(T::kRealtimeAudio);
  int nice;
  if (errno != ENOTSUP || alos_thread_get_nice(&nice) || nice != -10 ||
      base::PlatformThread::GetCurrentThreadType() != T::kInteractive ||
      base::PlatformThread::CanChangeThreadType(T::kDefault, T::kRealtimeAudio)) {
    puts("chromium-thread-smoke: FAIL unsupported realtime");
    return 1;
  }
  base::PlatformThread::SetCurrentThreadType(T::kDefault);
  Worker worker;
  base::PlatformThreadHandle handle;
  if (!base::PlatformThread::CreateWithType(0, &worker, &handle, T::kBackground)) {
    puts("chromium-thread-smoke: FAIL pthread creation");
    return 1;
  }
  base::PlatformThread::Join(handle);
  errno = 0;
  if (base::PlatformThread::CreateWithType(1, &worker, &handle, T::kDefault) ||
      errno != EINVAL || worker.calls != 1) {
    puts("chromium-thread-smoke: FAIL invalid stack accepted");
    return 1;
  }
  errno = 0;
  if (!worker.passed ||
      base::PlatformThread::CreateWithType(0, &worker, &handle, T::kRealtimeAudio) ||
      errno != ENOTSUP) {
    puts("chromium-thread-smoke: FAIL worker/realtime creation");
    return 1;
  }
  puts("chromium-thread-smoke: PASS");
  return 0;
}
