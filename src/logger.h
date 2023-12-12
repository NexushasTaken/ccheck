#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>

#define PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) \
  __attribute__ ((format (printf, STRING_INDEX, FIRST_TO_CHECK)))

void AVLOG(FILE *stream, int margin, const char *tag, const char *end, const char *fmt, ...);
#define AINFO(margin, end, ...) AVLOG(stderr, margin, "INFO", end, __VA_ARGS__)

void VLOG(FILE *stream, const char *tag, const char *fmt, va_list args);
void INFO(const char *fmt, ...) PRINTF_FORMAT(1, 2);
void WARN(const char *fmt, ...) PRINTF_FORMAT(1, 2);
void ERRO(const char *fmt, ...) PRINTF_FORMAT(1, 2);
void PANIC(const char *fmt, ...) PRINTF_FORMAT(1, 2);

#define ASSERT_NULL(var, ...) \
  do {                        \
    if ((var) == NULL) {      \
      PANIC(__VA_ARGS__);     \
    }                         \
  } while (0)

#define ASSERT_ERR(var, ...) \
  do {                       \
    if ((var) < 0) {        \
      PANIC(__VA_ARGS__);    \
    }                        \
  } while (0)

#define PANIC_IF(condition, ...) \
  do {                        \
    if ((condition)) {      \
      PANIC(__VA_ARGS__);     \
    }                         \
  } while (0)

#endif // LOGGER_H
