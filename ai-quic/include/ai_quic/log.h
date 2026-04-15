#ifndef AI_QUIC_LOG_H
#define AI_QUIC_LOG_H

typedef enum ai_quic_log_level {
  AI_QUIC_LOG_ERROR = 0,
  AI_QUIC_LOG_WARN = 1,
  AI_QUIC_LOG_INFO = 2,
  AI_QUIC_LOG_DEBUG = 3
} ai_quic_log_level_t;

void ai_quic_log_set_level(ai_quic_log_level_t level);
ai_quic_log_level_t ai_quic_log_get_level(void);
void ai_quic_log_write(ai_quic_log_level_t level,
                       const char *component,
                       const char *format,
                       ...);

#endif
