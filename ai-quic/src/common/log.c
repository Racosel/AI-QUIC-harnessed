#include "ai_quic/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static ai_quic_log_level_t ai_quic_log_level_current = AI_QUIC_LOG_INFO;

static const char *ai_quic_log_level_to_string(ai_quic_log_level_t level) {
  switch (level) {
    case AI_QUIC_LOG_ERROR:
      return "ERROR";
    case AI_QUIC_LOG_WARN:
      return "WARN";
    case AI_QUIC_LOG_INFO:
      return "INFO";
    case AI_QUIC_LOG_DEBUG:
      return "DEBUG";
    default:
      return "UNKNOWN";
  }
}

void ai_quic_log_set_level(ai_quic_log_level_t level) {
  ai_quic_log_level_current = level;
}

ai_quic_log_level_t ai_quic_log_get_level(void) {
  return ai_quic_log_level_current;
}

void ai_quic_log_write(ai_quic_log_level_t level,
                       const char *component,
                       const char *format,
                       ...) {
  time_t now;
  struct tm *tm_now;
  char timestamp[32];
  va_list args;

  if (level > ai_quic_log_level_current) {
    return;
  }

  now = time(NULL);
  tm_now = localtime(&now);
  if (tm_now == NULL) {
    return;
  }

  if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_now) == 0u) {
    return;
  }

  fprintf(stderr,
          "[%s] [%s] [%s] ",
          timestamp,
          ai_quic_log_level_to_string(level),
          component != NULL ? component : "ai_quic");

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputc('\n', stderr);
  fflush(stderr);
}
