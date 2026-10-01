#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#define PAGE 4096
#define CHECK(test) do { \
  if (!(test)) { \
    printf("uname-test: FAIL line=%d errno=%d\n", __LINE__, errno); \
    return 1; \
  } \
} while (0)

static int valid_field(const char *field) {
  return field[0] && memchr(field, '\0', ALOS_UTSNAME_FIELD_SIZE) != NULL;
}

int main(void) {
  CHECK(sysconf(_SC_NPROCESSORS_CONF) == 1);
  CHECK(sysconf(_SC_NPROCESSORS_ONLN) == 1);
  errno = 0;
  CHECK(sysconf(-1) == -1 && errno == EINVAL);
  CHECK(getpagesize() == 4096 && getpagesize() == sysconf(_SC_PAGESIZE));
  struct utsname name;
  CHECK(sizeof(name) == 325);
  CHECK(offsetof(struct utsname, nodename) == 65);
  CHECK(offsetof(struct utsname, release) == 130);
  CHECK(offsetof(struct utsname, version) == 195);
  CHECK(offsetof(struct utsname, machine) == 260);
  memset(&name, 0xa5, sizeof(name));
  CHECK(uname(&name) == 0);
  CHECK(valid_field(name.sysname) && valid_field(name.nodename) &&
        valid_field(name.release) && valid_field(name.version) &&
        valid_field(name.machine));
  CHECK(strcmp(name.sysname, "ALOS") == 0);
  CHECK(strcmp(name.nodename, "alos") == 0);
  CHECK(strcmp(name.release, "development") == 0);
  CHECK(strcmp(name.version, "ALOS development") == 0);
  CHECK(strcmp(name.machine, "x86_64") == 0);
  FILE *fixture = fopen("/metadata-fixture/payload", "r");
  CHECK(fixture != NULL);
  char line[32];
  CHECK(fgets(line, 1, fixture) == line && line[0] == '\0');
  CHECK(fgets(line, 5, fixture) == line && !strcmp(line, "meta"));
  CHECK(ungetc('X', fixture) == 'X');
  CHECK(fgets(line, sizeof(line), fixture) == line &&
        !strcmp(line, "Xdata-fixture\n"));
  CHECK(fgets(line, sizeof(line), fixture) == NULL && feof(fixture) &&
        !ferror(fixture));
  CHECK(fclose(fixture) == 0);
  FILE invalid = {.fd = -1};
  errno = 0;
  CHECK(fgets(line, sizeof(line), &invalid) == NULL &&
        errno == EBADF && ferror(&invalid));
  CHECK(strstr(name.release, "Linux") == NULL &&
        strstr(name.version, "Linux") == NULL);
  errno = 0;
  CHECK(uname(NULL) == -1 && errno == EFAULT);
  CHECK(syscall1(ALOS_SYS_UNAME, 0) == -EFAULT);
  errno = 0;
  CHECK(uname((struct utsname *)(uintptr_t)UINT64_MAX) == -1 &&
        errno == EFAULT);

  /* Deux pages non residentes : la copie doit declencher le fault-in. */
  char *pages = mmap(NULL, 2 * PAGE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(pages != MAP_FAILED);
  struct utsname *crossing = (struct utsname *)(pages + PAGE - 100);
  CHECK(uname(crossing) == 0);
  CHECK(memcmp(crossing, &name, sizeof(name)) == 0);
  CHECK(mprotect(pages + PAGE, PAGE, PROT_READ) == 0);
  errno = 0;
  CHECK(uname(crossing) == -1 && errno == EFAULT);
  CHECK(mprotect(pages, 2 * PAGE, PROT_NONE) == 0);
  errno = 0;
  CHECK(uname((struct utsname *)pages) == -1 && errno == EFAULT);
  CHECK(munmap(pages, 2 * PAGE) == 0);
  errno = 0;
  CHECK(uname((struct utsname *)pages) == -1 && errno == EFAULT);
  puts("uname-test: PASS");
  return 0;
}
