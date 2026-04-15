#ifndef AI_QUIC_TLS_H
#define AI_QUIC_TLS_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/packet.h"
#include "ai_quic/result.h"
#include "ai_quic/transport_params.h"

typedef struct ai_quic_tls_ctx ai_quic_tls_ctx_t;
typedef struct ai_quic_tls_session ai_quic_tls_session_t;

typedef enum ai_quic_tls_event_type {
  AI_QUIC_TLS_EVENT_NONE = 0,
  AI_QUIC_TLS_EVENT_WRITE_CRYPTO = 1,
  AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS = 2,
  AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS = 3,
  AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE = 4
} ai_quic_tls_event_type_t;

typedef struct ai_quic_tls_event {
  ai_quic_tls_event_type_t type;
  ai_quic_encryption_level_t level;
  size_t crypto_data_len;
  uint8_t crypto_data[AI_QUIC_MAX_FRAME_PAYLOAD_LEN];
} ai_quic_tls_event_t;

ai_quic_tls_ctx_t *ai_quic_tls_ctx_create(const char *cert_root);
void ai_quic_tls_ctx_destroy(ai_quic_tls_ctx_t *tls_ctx);
ai_quic_result_t ai_quic_tls_self_check(void);
const char *ai_quic_tls_backend_name(void);

ai_quic_tls_session_t *ai_quic_tls_session_create(ai_quic_tls_ctx_t *tls_ctx,
                                                  int is_server,
                                                  const char *alpn,
                                                  const char *keylog_path);
void ai_quic_tls_session_destroy(ai_quic_tls_session_t *session);
ai_quic_result_t ai_quic_tls_session_start(ai_quic_tls_session_t *session,
                                           const ai_quic_transport_params_t *local_params,
                                           const char *server_name);
ai_quic_result_t ai_quic_tls_session_provide_crypto(
    ai_quic_tls_session_t *session,
    ai_quic_encryption_level_t level,
    const uint8_t *data,
    size_t data_len);
ai_quic_result_t ai_quic_tls_session_poll_event(ai_quic_tls_session_t *session,
                                                ai_quic_tls_event_t *event);
const ai_quic_transport_params_t *ai_quic_tls_session_peer_transport_params(
    const ai_quic_tls_session_t *session);
int ai_quic_tls_session_handshake_complete(
    const ai_quic_tls_session_t *session);
ai_quic_result_t ai_quic_tls_session_get_packet_secret(
    const ai_quic_tls_session_t *session,
    ai_quic_encryption_level_t level,
    int is_write,
    uint32_t *cipher_suite,
    uint8_t *secret,
    size_t *secret_len,
    size_t secret_capacity);

#endif
