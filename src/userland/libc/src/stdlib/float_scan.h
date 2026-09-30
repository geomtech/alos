#ifndef ALOS_FLOAT_SCAN_H
#define ALOS_FLOAT_SCAN_H

#include <stddef.h>

typedef struct {
  const char *start;
  const char *cursor;
  int last;
  const char *end;
} FloatStream;

static inline int shgetc(FloatStream *f) {
  if ((f->end && f->cursor == f->end) || !*f->cursor) { f->last = -1; return -1; }
  f->last = (unsigned char)*f->cursor++;
  return f->last;
}
static inline void shunget(FloatStream *f) {
  if (f->last >= 0 && f->cursor > f->start) --f->cursor;
  f->last = 0;
}
static inline void shlim(FloatStream *f, int unused) {
  (void)unused;
  f->cursor = f->start;
  f->last = -1;
}
static inline size_t shcnt(FloatStream *f) { return (size_t)(f->cursor - f->start); }

long double __alos_floatscan(FloatStream *f, int precision, int partial_ok);
#endif
