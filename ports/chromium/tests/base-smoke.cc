#include "base/time/time.h"

#include <stdio.h>
#include <time.h>

int main() {
  const base::Time wall = base::Time::Now();
  const base::TimeTicks before = base::TimeTicks::Now();
  const struct timespec delay = {0, 20000000};
  if (wall.ToTimeT() < 1700000000 || before.is_null() ||
      nanosleep(&delay, nullptr) != 0) {
    puts("chromium-base-smoke: FAIL initial clocks");
    return 1;
  }
  const base::TimeDelta elapsed = base::TimeTicks::Now() - before;
  if (elapsed.InMilliseconds() < 20 ||
      base::Time::Now() < wall ||
      base::Seconds(1.25).InMicroseconds() != 1250000) {
    puts("chromium-base-smoke: FAIL time contracts");
    return 1;
  }
  puts("chromium-base-smoke: Time PASS");
  return 0;
}
