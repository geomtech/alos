#include "base/atomicops.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {
int failures;

void Check(bool condition, int line) {
  if (!condition) {
    printf("nativeatomic-runtime-test: FAIL line %d\n", line);
    ++failures;
  }
}
#define CHECK_NATIVE(condition) Check((condition), __LINE__)

void CheckCopies() {
  base::subtle::RelaxedAtomicWriteMemcpy(base::span<uint8_t>(),
                                        base::span<const uint8_t>());
  alignas(8) uint8_t source[160];
  alignas(8) uint8_t destination[160];
  for (size_t i = 0; i < sizeof(source); ++i)
    source[i] = static_cast<uint8_t>(i * 37 + 11);
  for (size_t src_offset = 0; src_offset < 16; ++src_offset) {
    for (size_t dst_offset = 0; dst_offset < 16; ++dst_offset) {
      for (size_t size = 0; size <= 129; ++size) {
        memset(destination, 0xc3, sizeof(destination));
        base::subtle::RelaxedAtomicWriteMemcpy(
            base::span<uint8_t>(destination + dst_offset, size),
            base::span<const uint8_t>(source + src_offset, size));
        CHECK_NATIVE(!memcmp(destination + dst_offset, source + src_offset, size));
        for (size_t i = 0; i < sizeof(destination); ++i)
          if (i < dst_offset || i >= dst_offset + size)
            CHECK_NATIVE(destination[i] == 0xc3);
      }
    }
  }
}

struct SharedCopy {
  alignas(8) uintmax_t destination = UINT64_C(0x5555555555555555);
  alignas(8) const uintmax_t first = UINT64_C(0x5555555555555555);
  alignas(8) const uintmax_t second = UINT64_C(0xaaaaaaaaaaaaaaaa);
  unsigned ready = 0, done = 0;
};

void CopyWord(SharedCopy* shared, const uintmax_t* source) {
  base::subtle::RelaxedAtomicWriteMemcpy(
      base::span<uint8_t>(reinterpret_cast<uint8_t*>(&shared->destination),
                          sizeof(shared->destination)),
      base::span<const uint8_t>(reinterpret_cast<const uint8_t*>(source),
                                sizeof(*source)));
}

void* Writer(void* argument) {
  auto* shared = static_cast<SharedCopy*>(argument);
  while (!__atomic_load_n(&shared->ready, __ATOMIC_ACQUIRE)) {}
  for (unsigned i = 0; i < 20000; ++i) {
    CopyWord(shared, &shared->first);
    CopyWord(shared, &shared->second);
  }
  // Relaxed copies do not publish completion; this separate release does.
  __atomic_store_n(&shared->done, 1, __ATOMIC_RELEASE);
  return nullptr;
}

void CheckConcurrentCopies() {
  SharedCopy shared;
  pthread_t writer;
  int error = pthread_create(&writer, nullptr, Writer, &shared);
  CHECK_NATIVE(error == 0);
  if (error) return;
  __atomic_store_n(&shared.ready, 1, __ATOMIC_RELEASE);
  bool untorn = true;
  do {
    uintmax_t value = __atomic_load_n(&shared.destination, __ATOMIC_RELAXED);
    if (value != shared.first && value != shared.second) untorn = false;
  } while (!__atomic_load_n(&shared.done, __ATOMIC_ACQUIRE));
  CHECK_NATIVE(untorn);
  CHECK_NATIVE(shared.destination == shared.second);
  CHECK_NATIVE(pthread_join(writer, nullptr) == 0);
}
}  // namespace

int main() {
  CheckCopies();
  CheckConcurrentCopies();
  if (failures) return 1;
  puts("nativeatomic-runtime-test: PASS");
  return 0;
}
