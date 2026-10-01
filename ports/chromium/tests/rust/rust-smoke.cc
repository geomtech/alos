#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

extern "C" uint32_t alos_rust_smoke_thread(uint32_t seed);

struct ThreadResult {
  uint32_t seed;
  uint32_t value;
};

static void* run_thread(void* arg) {
  ThreadResult* result = static_cast<ThreadResult*>(arg);
  result->value = alos_rust_smoke_thread(result->seed);
  return nullptr;
}

int main() {
  ThreadResult a = {11, 0};
  ThreadResult b = {29, 0};
  pthread_t ta;
  pthread_t tb;
  if (pthread_create(&ta, nullptr, run_thread, &a) != 0 ||
      pthread_create(&tb, nullptr, run_thread, &b) != 0) {
    printf("chromium-rust-smoke: FAIL pthread_create\n");
    return 1;
  }
  pthread_join(ta, nullptr);
  pthread_join(tb, nullptr);
  uint32_t main_value = alos_rust_smoke_thread(7);
  if (a.value != 45 || b.value != 99 || main_value != 33) {
    printf("chromium-rust-smoke: FAIL values a=%u b=%u main=%u\n", a.value,
           b.value, main_value);
    return 1;
  }
  printf("chromium-rust-smoke: PASS a=%u b=%u main=%u\n", a.value, b.value,
         main_value);
  return 0;
}