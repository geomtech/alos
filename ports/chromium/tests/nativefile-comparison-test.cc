#include "base/files/file_path.h"
#include "base/files/file_util.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace {
int failures;
const char* const kFirst = "/posix-test/chromium-compare-first";
const char* const kSecond = "/posix-test/chromium-compare-second";

void Check(bool condition, int line) {
  if (!condition) {
    printf("nativefile-comparison-test: FAIL line %d\n", line);
    ++failures;
  }
}
#define CHECK_NATIVE(condition) Check((condition), __LINE__)

bool WriteFixture(const char* path, const char* bytes, size_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return false;
  size_t offset = 0;
  while (offset < size) {
    ssize_t written = write(fd, bytes + offset, size - offset);
    if (written <= 0) {
      close(fd);
      unlink(path);
      return false;
    }
    offset += static_cast<size_t>(written);
  }
  return close(fd) == 0;
}

void Compare(const char* first, size_t size1, const char* second, size_t size2,
             bool binary, bool text) {
  const bool created1 = WriteFixture(kFirst, first, size1);
  const bool created2 = WriteFixture(kSecond, second, size2);
  CHECK_NATIVE(created1 && created2);
  if (created1 && created2) {
    base::FilePath path1(kFirst), path2(kSecond);
    CHECK_NATIVE(base::ContentsEqual(path1, path2) == binary);
    CHECK_NATIVE(base::TextContentsEqual(path1, path2) == text);
    CHECK_NATIVE(base::ContentsEqual(path2, path1) == binary);
    CHECK_NATIVE(base::TextContentsEqual(path2, path1) == text);
  }
  if (created1)
    CHECK_NATIVE(unlink(kFirst) == 0);
  if (created2)
    CHECK_NATIVE(unlink(kSecond) == 0);
}
}

int main() {
  Compare("", 0, "", 0, true, true);
  Compare("abc", 3, "abc", 3, true, true);
  Compare("abc", 3, "abcd", 4, false, false);
  Compare("abc\n", 4, "abc", 3, false, false);
  Compare("abc\r\n", 5, "abc\n", 4, false, true);
  Compare("abc\r\r", 5, "abc", 3, false, true);
  Compare("a\rb\n", 4, "ab\n", 3, false, false);
  Compare("a\r\rb\n", 5, "a\r\rb\n", 5, true, true);
  Compare("\r\r\n", 3, "\n", 1, false, true);
  Compare("\r\r", 2, "", 0, false, true);
  Compare("\n", 1, "", 0, false, false);
  Compare(" \n", 2, "\n", 1, false, false);
  Compare("a\0b\n", 4, "a\0c\n", 4, false, false);
  Compare("a\n\n", 3, "a\n", 2, false, false);
  char first[12339], second[12340];
  memset(first, 'x', sizeof(first));
  memcpy(second, first, sizeof(first));
  first[sizeof(first) - 1] = '\n';
  second[sizeof(first) - 1] = '\r';
  second[sizeof(second) - 1] = '\n';
  Compare(first, sizeof(first), second, sizeof(second), false, true);
  Compare(first, 2056, first, 2056, true, true);
  Compare(first, 4112, first, 4112, true, true);
  second[9000] = 'y';
  Compare(first, sizeof(first), second, sizeof(second), false, false);

  const base::FilePath absent("/posix-test/chromium-compare-absent");
  const base::FilePath directory("/posix-test");
  CHECK_NATIVE(!base::ContentsEqual(absent, absent));
  CHECK_NATIVE(!base::TextContentsEqual(absent, absent));
  CHECK_NATIVE(!base::ContentsEqual(directory, directory));
  CHECK_NATIVE(!base::TextContentsEqual(directory, directory));
  printf("nativefile-comparison-test: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
