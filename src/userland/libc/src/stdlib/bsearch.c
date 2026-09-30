#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
  if (!count) return NULL;
  if (!key || !base || !compare || !size) { errno = EINVAL; return NULL; }
  if (count > SIZE_MAX / size) { errno = EOVERFLOW; return NULL; }
  const unsigned char *first = base;
  while (count) {
    size_t middle = count / 2;
    const void *element = first + middle * size;
    int order = compare(key, element);
    if (!order) return (void *)element;
    if (order < 0) count = middle;
    else {
      first += (middle + 1) * size;
      count -= middle + 1;
    }
  }
  return NULL;
}
