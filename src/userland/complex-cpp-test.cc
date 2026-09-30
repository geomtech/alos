#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/mman.h>

static std::atomic<int> global_destructors{0};
struct Global {
  ~Global() {
    if (++global_destructors == 1) puts("complex-cpp-test: GLOBAL DTOR PASS");
    else puts("complex-cpp-test: FAIL global destructor");
  }
} global;
static std::atomic<int> tls_destructors{0};
struct Local { int value = 7; ~Local() { ++tls_destructors; } };
thread_local Local local;
static std::mutex mutex;
static std::recursive_mutex recursive;
static std::condition_variable cv;
static bool go;
static std::atomic<int> count{0};
static std::atomic<int> failed{0};

static void worker() {
  if (local.value != 7) ++failed;
  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [] { return go; });
  }
  for (int i = 0; i < 10000; ++i) {
    std::lock_guard<std::recursive_mutex> lock(recursive);
    ++count;
  }
}
int main() {
  std::string name = "ALOS";
  name += " Chromium";
  std::vector<int> values{1, 2, 3, 4};
  auto owned = std::make_unique<int>(7);
  auto shared = std::make_shared<int>(9);
  if (name != "ALOS Chromium" || values.size() != 4 ||
      *owned != 7 || *shared != 9) ++failed;
  puts("complex-cpp-test: strings containers PASS");
  auto start = std::chrono::steady_clock::now();
  std::thread a(worker), b(worker), c(worker), d(worker);
  {
    std::lock_guard<std::mutex> lock(mutex);
    go = true;
  }
  cv.notify_all();
  a.join(); b.join(); c.join(); d.join();
  if (count != 40000 || tls_destructors != 4 ||
      std::chrono::steady_clock::now() < start) ++failed;
  puts("complex-cpp-test: atomics threads mutex condvar chrono tls PASS");
  void *p = aligned_alloc(64, 256);
  void *map = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!p || map == MAP_FAILED || mprotect(map, 4096, PROT_READ) ||
      munmap(map, 4096)) ++failed;
  free(p);
  timespec delay{0, 1000000};
  if (nanosleep(&delay, nullptr)) ++failed;
  if (failed) { puts("complex-cpp-test: FAIL"); return 1; }
  puts("complex-cpp-test: memory destructors PASS");
  puts("complex-cpp-test: ALL PASS");
  return 0;
}
