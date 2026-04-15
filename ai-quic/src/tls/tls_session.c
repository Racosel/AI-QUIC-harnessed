#include "tls_internal.h"

#include <string.h>

extern ai_quic_result_t ai_quic_tls_log_handshake_keys(ai_quic_tls_session_t *session);
extern ai_quic_result_t ai_quic_tls_log_application_keys(ai_quic_tls_session_t *session);

ai_quic_result_t ai_quic_tls_session_maybe_log_keys(ai_quic_tls_session_t *session,
                                                    ai_quic_tls_event_type_t type) {
  if (session == NULL || session->mode != AI_QUIC_TLS_MODE_FAKE) {
    return AI_QUIC_OK;
  }
  if (type == AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS) {
    return ai_quic_tls_log_handshake_keys(session);
  }
  if (type == AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS) {
    return ai_quic_tls_log_application_keys(session);
  }
  return AI_QUIC_OK;
}
