#include <time.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define CHECK(test) do { if (!(test)) { \
  printf("calendar-test: FAIL line %d\n", __LINE__); return 1; \
} } while (0)

int main(void)
{
  struct tm date = {0};
  date.tm_isdst = -1;
  date.tm_gmtoff = 123;
  date.tm_zone = "sentinel";
  const char *input = "2000-02-29 23:59:60tail";
  char *end = strptime(input, "%F %T", &date);
  CHECK(end == input + 19 && !strcmp(end, "tail"));
  CHECK(date.tm_year == 100 && date.tm_mon == 1 && date.tm_mday == 29);
  CHECK(date.tm_hour == 23 && date.tm_min == 59 && date.tm_sec == 60);
  CHECK(date.tm_isdst == -1 && date.tm_gmtoff == 123 &&
        !strcmp(date.tm_zone, "sentinel"));
  struct tm before = date;
  CHECK(strptime("07!", "%M", &date) && date.tm_min == 7);
  before.tm_min = 7;
  CHECK(!memcmp(&date, &before, sizeof(date)));
  CHECK(strptime("Sun Feb 29 00:01:02 2004", "%c", &date));
  CHECK(date.tm_wday == 0 && date.tm_mon == 1 && date.tm_mday == 29 &&
        date.tm_year == 104 && date.tm_sec == 2);
  CHECK(strptime("02/29/00 11:42:03 PM", "%x %r", &date));
  CHECK(date.tm_year == 100 && date.tm_hour == 23);
  CHECK(strptime("12:01:02 AM", "%r", &date) && date.tm_hour == 0);
  CHECK(strptime("tuesday SEPTEMBER", "%A %B", &date) &&
        date.tm_wday == 2 && date.tm_mon == 8);
  CHECK(strptime("68", "%y", &date) && date.tm_year == 168);
  CHECK(strptime("69", "%y", &date) && date.tm_year == 69);
  CHECK(strptime("20 24", "%C %y", &date) && date.tm_year == 124);
  CHECK(strptime("24 19", "%y %C", &date) && date.tm_year == 24);
  CHECK(strptime("366 7", "%j %u", &date) &&
        date.tm_yday == 365 && date.tm_wday == 0);
  before = date;
  CHECK(strptime("2020-W53", "%G-W%V", &date));
  CHECK(!memcmp(&before, &date, sizeof(date)));
  CHECK(!strptime("2020-W00", "%G-W%V", &date));
  CHECK(!strptime("54", "%U", &date));
  CHECK(strptime("2024-02-29", "%EY-%Om-%Od", &date));
  CHECK(strptime(" \t%\n12:34", "%n%%%t%R", &date));
  CHECK(!strptime("13", "%m", &date));
  CHECK(!strptime("00", "%d", &date));
  CHECK(!strptime("24", "%H", &date));
  CHECK(!strptime("60", "%M", &date));
  CHECK(!strptime("61", "%S", &date));
  CHECK(!strptime("367", "%j", &date));
  CHECK(!strptime("2000", "%", &date));
  CHECK(!strptime("2000", "%Q", &date));
  CHECK(!strptime("2000", "%OY", &date));
  CHECK(!strptime("2000", "%999999999999999999Y", &date));
  CHECK(!strptime("999999999999", "%12Y", &date));
  CHECK(!strptime("21474856 00", "%8C %y", &date));
  CHECK(strptime("2147485547", "%10Y", &date) && date.tm_year == INT_MAX);
  CHECK(!strptime("2147485548", "%10Y", &date));
  CHECK(strptime("2000-02-29!", "%10F", &date));
  input = "2000-02-29";
  CHECK(strptime(input, "%9F", &date) == input + 9 && date.tm_mday == 2);
  CHECK(!strptime(input, "%8F", &date));
  CHECK(strptime("+0530", "%z", &date) && date.tm_gmtoff == 19800);
  CHECK(strptime("-0030", "%z", &date) && date.tm_gmtoff == -1800);
  CHECK(!strptime("+2400", "%z", &date));
  CHECK(!strptime("+1260", "%z", &date));
  CHECK(!strptime("+123", "%z", &date));
  CHECK(strptime("UTC!", "%Z", &date) && date.tm_gmtoff == 0 &&
        date.tm_isdst == 0 && !strcmp(date.tm_zone, "UTC"));
  CHECK(strptime("GMT", "%Z", &date));
  CHECK(!strptime("PST", "%Z", &date));
  CHECK(!strptime("UTCfoo", "%Z", &date));
  tzset();
  CHECK(timezone == 0 && daylight == 0 &&
        !strcmp(tzname[0], "UTC") && !strcmp(tzname[1], "UTC"));
  time_t epoch = 951782400;
  CHECK(localtime_r(&epoch, &date) && date.tm_year == 100 &&
        date.tm_mon == 1 && date.tm_mday == 29 &&
        date.tm_yday == 59 && date.tm_wday == 2 &&
        date.tm_isdst == 0 && date.tm_gmtoff == 0);
  CHECK(mktime(&date) == epoch);
  char output[64];
  CHECK(strftime(output, sizeof(output), "%F %z %Z", &date) &&
        !strcmp(output, "2000-02-29 +0000 UTC"));
  puts("calendar-test: PASS (C parsing, ranges, endptr, UTC profile)");
  return 0;
}
