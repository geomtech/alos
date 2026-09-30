#ifndef ALOS_PRINTF_SINK_H
#define ALOS_PRINTF_SINK_H

#include <stdio.h>

struct __alos_sink {
  FILE *stream;
  char *buffer;
  size_t capacity;
  size_t used;
  int error;
};

void __alos_sink_write(struct __alos_sink *sink, const char *data, size_t length);
int __alos_vformat(struct __alos_sink *sink, const char *format, va_list arguments);

#endif
