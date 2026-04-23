#ifndef AI_QUIC_TLS_INTERNAL_H
#define AI_QUIC_TLS_INTERNAL_H

#include <stdio.h>

#include <openssl/ssl.h>

#include "ai_quic/tls.h"

#define AI_QUIC_TLS_MAX_EVENTS 8u
#define AI_QUIC_TLS_MAX_SECRET_LEN 64u

typedef enum ai_quic_tls_mode {
  AI_QUIC_TLS_MODE_FAKE = 0,
  AI_QUIC_TLS_MODE_BORINGSSL_QUIC = 1
} ai_quic_tls_mode_t;

typedef enum ai_quic_tls_state {
  AI_QUIC_TLS_STATE_IDLE = 0,
  AI_QUIC_TLS_STATE_WAIT_CLIENT_HELLO = 1,
  AI_QUIC_TLS_STATE_WAIT_SERVER_HELLO = 2,
  AI_QUIC_TLS_STATE_WAIT_CLIENT_FINISHED = 3,
  AI_QUIC_TLS_STATE_COMPLETE = 4
} ai_quic_tls_state_t;

struct ai_quic_tls_ctx {
  char cert_root[512];
};

typedef struct ai_quic_tls_level_secret {
  int valid;
  uint32_t cipher_suite;
  size_t secret_len;
  uint8_t secret[AI_QUIC_TLS_MAX_SECRET_LEN];
} ai_quic_tls_level_secret_t;

typedef struct ai_quic_tls_boringssl {
  SSL_CTX *ssl_ctx;
  SSL *ssl;
  ai_quic_tls_level_secret_t read_levels[4];
  ai_quic_tls_level_secret_t write_levels[4];
  int peer_params_loaded;
  int handshake_complete_reported;
  char last_error[256];
} ai_quic_tls_boringssl_t;

typedef struct ai_quic_tls_fake {
  ai_quic_tls_state_t state;
  int handshake_keys_installed;
  int one_rtt_keys_installed;
} ai_quic_tls_fake_t;

typedef union ai_quic_tls_backend_state {
  ai_quic_tls_boringssl_t boring;
  ai_quic_tls_fake_t fake;
} ai_quic_tls_backend_state_t;

struct ai_quic_tls_session {
  ai_quic_tls_ctx_t *ctx;
  int is_server;
  ai_quic_tls_mode_t mode;
  ai_quic_tls_cipher_policy_t cipher_policy;
  ai_quic_transport_params_t local_params;
  ai_quic_transport_params_t peer_params;
  uint8_t local_params_wire[512];
  size_t local_params_wire_len;
  ai_quic_tls_event_t events[AI_QUIC_TLS_MAX_EVENTS];
  size_t event_head;
  size_t event_len;
  FILE *keylog_file;
  char alpn[64];
  char server_name[256];
  int handshake_complete;
  int handshake_keys_installed;
  int one_rtt_keys_installed;
  ai_quic_tls_backend_state_t backend;
};

void ai_quic_tls_queue_event(ai_quic_tls_session_t *session,
                             const ai_quic_tls_event_t *event);
ai_quic_result_t ai_quic_tls_queue_crypto(ai_quic_tls_session_t *session,
                                          ai_quic_encryption_level_t level,
                                          const uint8_t *data,
                                          size_t data_len);
ai_quic_result_t ai_quic_tls_write_keylog(ai_quic_tls_session_t *session,
                                          const char *label,
                                          const uint8_t *secret,
                                          size_t secret_len);
ai_quic_result_t ai_quic_tls_generate_initial_secret(const uint8_t *seed,
                                                     size_t seed_len,
                                                     uint8_t *secret,
                                                     size_t secret_len);
ai_quic_result_t ai_quic_tls_session_maybe_log_keys(
    ai_quic_tls_session_t *session,
    ai_quic_tls_event_type_t type);
void ai_quic_boringssl_set_aes_hw_override(SSL_CTX *ctx, int override_value);
void ai_quic_boringssl_set_tls13_chacha20_only(SSL_CTX *ctx, int enabled);

#endif
