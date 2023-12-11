#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "logger.h"

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
