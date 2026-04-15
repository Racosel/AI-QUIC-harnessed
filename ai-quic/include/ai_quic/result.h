#ifndef AI_QUIC_RESULT_H
#define AI_QUIC_RESULT_H

typedef enum ai_quic_result {
  AI_QUIC_OK = 0,
  AI_QUIC_ERROR = 1,
  AI_QUIC_UNSUPPORTED = 127
} ai_quic_result_t;

const char *ai_quic_result_to_string(ai_quic_result_t result);

#endif
