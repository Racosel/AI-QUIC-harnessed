#include "tls_internal.h"

#include <string.h>

static ai_quic_result_t ai_quic_tls_log_secret(ai_quic_tls_session_t *session,
                                               const char *label) {
  uint8_t secret[32];

  if (ai_quic_tls_generate_initial_secret((const uint8_t *)label,
                                          strlen(label),
                                          secret,
                                          sizeof(secret)) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  return ai_quic_tls_write_keylog(session, label, secret, sizeof(secret));
}

ai_quic_result_t ai_quic_tls_self_check(void);

/* Intentionally small wrappers keep secret generation centralized. */
ai_quic_result_t ai_quic_tls_log_handshake_keys(ai_quic_tls_session_t *session) {
  return ai_quic_tls_log_secret(session, "SERVER_HANDSHAKE_TRAFFIC_SECRET");
}

ai_quic_result_t ai_quic_tls_log_application_keys(ai_quic_tls_session_t *session) {
  return ai_quic_tls_log_secret(session, "SERVER_TRAFFIC_SECRET_0");
}
