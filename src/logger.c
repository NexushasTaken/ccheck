#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "logger.h"

static int count_digits(int num) {
  if (!num) {
    return 1;
  }

  num = abs(num);
  int count = 0;
  while (num > 0) {
    num /= 10;
    count += 1;
  }
  return count;
}

void AVLOG(FILE *stream, int margin, const char *tag, const char *end, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  int format_size = count_digits(margin) + 8;
  char *format = malloc(format_size);
  snprintf(format, format_size, "[%%s] %%%ds", margin);
  // printf("format: %s\n", format);

  fprintf(stream, format, tag, "");
  vfprintf(stream, fmt, args);
  fprintf(stream, "%s", end);

  va_end(args);
}

void VLOG(FILE *stream, const char *tag, const char *fmt, va_list args) {
  fprintf(stream, "[%s] ", tag);
  vfprintf(stream, fmt, args);
  fprintf(stream, "\n");
}

void INFO(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  VLOG(stderr, "INFO", fmt, args);
  va_end(args);
}

void WARN(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  VLOG(stderr, "WARN", fmt, args);
  va_end(args);
}

void ERRO(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  VLOG(stderr, "ERRO", fmt, args);
  va_end(args);
}

void PANIC(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  VLOG(stderr, "ERRO", fmt, args);
  va_end(args);
  exit(1);
}
