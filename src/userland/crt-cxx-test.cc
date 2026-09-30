#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
extern "C" void __libc_thread_fini(void);
static int phase;
struct Global {
  Global() { phase = 1; puts("GLOBAL CTOR"); }
  ~Global() { if (phase == 2) puts("GLOBAL DTOR"); else puts("crt-cxx-test: FAIL"); }
} global;
struct Local {
  int value;
  Local() : value(73) { puts("TLS CTOR"); }
  ~Local() { puts("TLS DTOR"); }
};
thread_local Local local;
int main() {
  if (phase != 1 || local.value != 73) return 1;
  assert(phase == 1);
  ldiv_t result = ldiv(-17L, 5L);
  lldiv_t large = lldiv(-INT64_C(1234567890123), 10);
  if (result.quot != -3 || result.rem != -2 ||
      large.quot != -INT64_C(123456789012) || large.rem != -3 ||
      llabs(-77) != 77 || sizeof(intmax_t) != 8 || CHAR_BIT != 8)
    return 1;
  volatile double zero = 0.0;
  volatile double normal = 1.25;
  volatile double infinite = HUGE_VAL;
  volatile double nan = NAN;
  if (fpclassify(zero) != FP_ZERO || fpclassify(normal) != FP_NORMAL ||
      !isinf(infinite) || !isnan(nan) || !isfinite(normal) ||
      !isunordered(nan, normal)) return 1;
  if (fprintf(stderr, "CRT STDERR %d\n", 42) != 14) return 1;
  if (!isatty(fileno(stdout))) return 1;
  if (fwrite("CRT FWRITE\n", 1, 11, stdout) != 11 ||
      fflush(stdout) || ferror(stdout)) return 1;
  FILE *probe = fopen("/config/startup.sh", "rb");
  char input[4096];
  if (!probe || fread(input, 1, sizeof(input), probe) == 0 ||
      !feof(probe) || ferror(probe)) return 1;
  if (isatty(fileno(probe)) != 0 || fclose(probe)) return 1;
  char *end;
  if (strtoull("18446744073709551615!", &end, 10) != UINT64_MAX ||
      *end != '!') return 1;
  errno = 0;
  if (strtoll("-9223372036854775809", &end, 10) != INT64_MIN ||
      errno != ERANGE || *end) return 1;
  const char *invalid = "0x";
  if (strtol(invalid, &end, 0) != 0 || end != invalid + 1) return 1;
  char error_text[64];
  if (strerror_r(EINVAL, error_text, sizeof(error_text)) ||
      strcmp(error_text, "Invalid argument")) return 1;
  if (creat("/crt-remove-probe", 0) || remove("/crt-remove-probe")) return 1;
  if (mkdir("/crt-remove-dir") || remove("/crt-remove-dir")) return 1;
  phase = 2;
  puts("MAIN");
  puts("crt-cxx-test: PASS");
  return 0;
}
