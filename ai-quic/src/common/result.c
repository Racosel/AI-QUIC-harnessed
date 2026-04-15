#include "ai_quic/result.h"

const char *ai_quic_result_to_string(ai_quic_result_t result) {
  switch (result) {
    case AI_QUIC_OK:
      return "ok";
    case AI_QUIC_ERROR:
      return "error";
    case AI_QUIC_UNSUPPORTED:
      return "unsupported";
    default:
      return "unknown";
  }
}
