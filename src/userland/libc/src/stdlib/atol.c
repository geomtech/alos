#include <stdlib.h>

long atol(const char *text) {
  return strtol(text, NULL, 10);
}
