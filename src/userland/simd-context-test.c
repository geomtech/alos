#include "runtime-test.h"
static uint32_t avx_workers;

static void worker(void *argument) {
  uint64_t id = (uint64_t)argument;
  uint64_t expected[4] __attribute__((aligned(32))) = {
      id * 101 + 7, id * 101 + 7, id * 101 + 7, id * 101 + 7};
  uint64_t observed[4] __attribute__((aligned(32)));
  uint32_t a, b, c, d;
  __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
  int avx = (c & (1U << 27)) && (c & (1U << 28));
  if (avx) {
    __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(0));
    avx = (a & 6) == 6;
  }
  if (avx) __atomic_add_fetch(&avx_workers, 1, __ATOMIC_RELAXED);
  uint32_t initial;
  __asm__ volatile("stmxcsr %0" : "=m"(initial));
  if (initial != 0x1f80) finish_worker(1);
  uint32_t rounding = 0x1f80 | ((id & 3) << 13);
  __asm__ volatile("ldmxcsr %0" :: "m"(rounding));
  double x87 = (double)id + 0.25;
  __asm__ volatile("fldl %0" :: "m"(x87));
  if (avx) __asm__ volatile("vmovdqu %0, %%ymm15" :: "m"(expected) : "ymm15");
  else __asm__ volatile("movdqu %0, %%xmm15" :: "m"(expected) : "xmm15");

  volatile double factor = 1.000000001;
  double sum = 1.0;
  uint64_t start = syscall0(SYS_CONTEXT_SWITCHES);
  for (;;) {
    for (unsigned i = 0; i < 4096; i++) sum = sum * factor + 0.000000001;
    if (avx) __asm__ volatile("vmovdqu %%ymm15, %0" : "=m"(observed));
    else __asm__ volatile("movdqu %%xmm15, %0" : "=m"(observed));
    double got;
    __asm__ volatile("fstl %0" : "=m"(got));
    __asm__ volatile("stmxcsr %0" : "=m"(initial));
    for (int i = 0; i < (avx ? 4 : 2); i++) {
      if (observed[i] != expected[i]) finish_worker(1);
    }
    if (got != x87 || (initial & 0x6000) != (rounding & 0x6000))
      finish_worker(1);
    if ((uint64_t)syscall0(SYS_CONTEXT_SWITCHES) - start >= 100) break;
  }
  __asm__ volatile("fstp %%st(0)" ::: "st");
  if (avx) __asm__ volatile("vzeroupper");
  if (!(sum > 1.0 && sum < 1000000.0)) finish_worker(1);
  finish_worker(0);
}

int main(void) {
  for (long i = 1; i <= 4; i++) if (!launch(worker, i)) return 1;
  if (!await_workers(4)) { puts("simd-context-test: FAIL"); return 1; }
  puts(avx_workers == 4
           ? "simd-context-test: PASS (400+ switches, x87/SSE/AVX, MXCSR)"
           : "simd-context-test: PASS (400+ switches, x87/SSE, MXCSR)");
  return 0;
}
