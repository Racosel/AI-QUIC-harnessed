#include "ai_quic/tls.h"

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ai_quic/log.h"
#include "tls_internal.h"

static const SSL_QUIC_METHOD kAiQuicMethod;

static const char *ai_quic_tls_event_type_name(ai_quic_tls_event_type_t type) {
  switch (type) {
    case AI_QUIC_TLS_EVENT_WRITE_CRYPTO:
      return "write_crypto";
    case AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS:
      return "install_handshake_keys";
    case AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS:
      return "install_1rtt_keys";
    case AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE:
      return "handshake_complete";
    case AI_QUIC_TLS_EVENT_NONE:
    default:
      return "none";
  }
}

static void ai_quic_tls_event_clear(ai_quic_tls_event_t *event) {
  if (event == NULL) {
    return;
  }
  memset(event, 0, sizeof(*event));
}

static void ai_quic_tls_set_error(ai_quic_tls_session_t *session,
                                  const char *format,
                                  ...) {
  va_list args;

  if (session == NULL || format == NULL ||
      session->mode != AI_QUIC_TLS_MODE_BORINGSSL_QUIC) {
    return;
  }

  va_start(args, format);
  vsnprintf(session->backend.boring.last_error,
            sizeof(session->backend.boring.last_error),
            format,
            args);
  va_end(args);
}

static int ai_quic_tls_file_exists(const char *path) {
  return path != NULL && path[0] != '\0' && access(path, F_OK) == 0;
}

static void ai_quic_tls_join_path(char *buffer,
                                  size_t capacity,
                                  const char *root,
                                  const char *leaf) {
  if (buffer == NULL || capacity == 0u) {
    return;
  }
  if (root == NULL || leaf == NULL) {
    buffer[0] = '\0';
    return;
  }
  snprintf(buffer, capacity, "%s/%s", root, leaf);
}

static ai_quic_encryption_level_t ai_quic_tls_from_ssl_level(
    enum ssl_encryption_level_t level) {
  switch (level) {
    case ssl_encryption_initial:
      return AI_QUIC_ENCRYPTION_INITIAL;
    case ssl_encryption_handshake:
      return AI_QUIC_ENCRYPTION_HANDSHAKE;
    case ssl_encryption_application:
    case ssl_encryption_early_data:
    default:
      return AI_QUIC_ENCRYPTION_APPLICATION;
  }
}

static enum ssl_encryption_level_t ai_quic_tls_to_ssl_level(
    ai_quic_encryption_level_t level) {
  switch (level) {
    case AI_QUIC_ENCRYPTION_INITIAL:
      return ssl_encryption_initial;
    case AI_QUIC_ENCRYPTION_HANDSHAKE:
      return ssl_encryption_handshake;
    case AI_QUIC_ENCRYPTION_APPLICATION:
    default:
      return ssl_encryption_application;
  }
}

void ai_quic_tls_queue_event(ai_quic_tls_session_t *session,
                             const ai_quic_tls_event_t *event) {
  size_t index;

  if (session == NULL || event == NULL) {
    return;
  }
  if (session->event_len >= AI_QUIC_TLS_MAX_EVENTS) {
    ai_quic_log_write(AI_QUIC_LOG_WARN,
                      "tls",
                      "drop tls event type=%s level=%d len=%zu queue_len=%zu is_server=%d",
                      ai_quic_tls_event_type_name(event->type),
                      (int)event->level,
                      event->crypto_data_len,
                      session->event_len,
                      session->is_server);
    return;
  }

  index = (session->event_head + session->event_len) % AI_QUIC_TLS_MAX_EVENTS;
  session->events[index] = *event;
  session->event_len += 1u;
  if (!session->is_server) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "tls",
                      "queued tls event type=%s level=%d len=%zu queue_len=%zu handshake_complete=%d",
                      ai_quic_tls_event_type_name(event->type),
                      (int)event->level,
                      event->crypto_data_len,
                      session->event_len,
                      session->handshake_complete);
  }
}

ai_quic_result_t ai_quic_tls_queue_crypto(ai_quic_tls_session_t *session,
                                          ai_quic_encryption_level_t level,
                                          const uint8_t *data,
                                          size_t data_len) {
  ai_quic_tls_event_t event;
  size_t offset;

  if (session == NULL || data == NULL) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  while (offset < data_len) {
    size_t chunk_len = data_len - offset;

    if (chunk_len > sizeof(event.crypto_data)) {
      chunk_len = sizeof(event.crypto_data);
    }

    ai_quic_tls_event_clear(&event);
    event.type = AI_QUIC_TLS_EVENT_WRITE_CRYPTO;
    event.level = level;
    event.crypto_data_len = chunk_len;
    memcpy(event.crypto_data, data + offset, chunk_len);
    ai_quic_tls_queue_event(session, &event);
    offset += chunk_len;
  }

  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_tls_emit_install_event(
    ai_quic_tls_session_t *session,
    ai_quic_tls_event_type_t type) {
  ai_quic_tls_event_t event;

  if (session == NULL) {
    return AI_QUIC_ERROR;
  }

  if (session->mode == AI_QUIC_TLS_MODE_FAKE &&
      ai_quic_tls_session_maybe_log_keys(session, type) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (type == AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS) {
    if (session->handshake_keys_installed) {
      return AI_QUIC_OK;
    }
    session->handshake_keys_installed = 1;
  } else if (type == AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS) {
    if (session->one_rtt_keys_installed) {
      return AI_QUIC_OK;
    }
    session->one_rtt_keys_installed = 1;
  }

  ai_quic_tls_event_clear(&event);
  event.type = type;
  ai_quic_tls_queue_event(session, &event);
  return AI_QUIC_OK;
}

static enum ssl_verify_result_t ai_quic_tls_verify_ok(SSL *ssl, uint8_t *out_alert) {
  (void)ssl;
  (void)out_alert;
  return ssl_verify_ok;
}

static void ai_quic_tls_keylog_callback(const SSL *ssl, const char *line) {
  ai_quic_tls_session_t *session;

  session = (ai_quic_tls_session_t *)SSL_get_app_data(ssl);
  if (session == NULL || session->keylog_file == NULL || line == NULL) {
    return;
  }

  fprintf(session->keylog_file, "%s\n", line);
  fflush(session->keylog_file);
}

static int ai_quic_tls_alpn_select_cb(SSL *ssl,
                                      const uint8_t **out,
                                      uint8_t *out_len,
                                      const uint8_t *in,
                                      unsigned in_len,
                                      void *arg) {
  ai_quic_tls_session_t *session;
  uint8_t *selected;
  uint8_t selected_len;
  uint8_t supported[64];
  size_t alpn_len;

  (void)ssl;
  session = (ai_quic_tls_session_t *)arg;
  if (session == NULL || session->alpn[0] == '\0') {
    return SSL_TLSEXT_ERR_NOACK;
  }

  alpn_len = strlen(session->alpn);
  if (alpn_len == 0u || alpn_len > 255u || alpn_len + 1u > sizeof(supported)) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  supported[0] = (uint8_t)alpn_len;
  memcpy(supported + 1u, session->alpn, alpn_len);
  if (SSL_select_next_proto(&selected,
                            &selected_len,
                            in,
                            in_len,
                            supported,
                            (unsigned)(alpn_len + 1u)) != OPENSSL_NPN_NEGOTIATED) {
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }

  *out = selected;
  *out_len = selected_len;
  return SSL_TLSEXT_ERR_OK;
}

static int ai_quic_tls_store_secret(ai_quic_tls_session_t *session,
                                    enum ssl_encryption_level_t level,
                                    const SSL_CIPHER *cipher,
                                    const uint8_t *secret,
                                    size_t secret_len,
                                    int is_write) {
  ai_quic_tls_level_secret_t *slot;

  if (session == NULL || session->mode != AI_QUIC_TLS_MODE_BORINGSSL_QUIC ||
      level > ssl_encryption_application || secret == NULL ||
      secret_len > sizeof(session->backend.boring.read_levels[0].secret)) {
    return 0;
  }

  slot = is_write ? &session->backend.boring.write_levels[level]
                  : &session->backend.boring.read_levels[level];
  memset(slot, 0, sizeof(*slot));
  slot->valid = 1;
  slot->cipher_suite =
      cipher != NULL ? SSL_CIPHER_get_protocol_id(cipher) : 0u;
  slot->secret_len = secret_len;
  memcpy(slot->secret, secret, secret_len);

  if (is_write && level == ssl_encryption_handshake &&
      ai_quic_tls_emit_install_event(session,
                                     AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS) !=
          AI_QUIC_OK) {
    return 0;
  }
  if (is_write && level == ssl_encryption_application &&
      ai_quic_tls_emit_install_event(session,
                                     AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS) !=
          AI_QUIC_OK) {
    return 0;
  }
  return 1;
}

static int ai_quic_tls_set_read_secret_cb(SSL *ssl,
                                          enum ssl_encryption_level_t level,
                                          const SSL_CIPHER *cipher,
                                          const uint8_t *secret,
                                          size_t secret_len) {
  return ai_quic_tls_store_secret((ai_quic_tls_session_t *)SSL_get_app_data(ssl),
                                  level,
                                  cipher,
                                  secret,
                                  secret_len,
                                  0);
}

static int ai_quic_tls_set_write_secret_cb(SSL *ssl,
                                           enum ssl_encryption_level_t level,
                                           const SSL_CIPHER *cipher,
                                           const uint8_t *secret,
                                           size_t secret_len) {
  return ai_quic_tls_store_secret((ai_quic_tls_session_t *)SSL_get_app_data(ssl),
                                  level,
                                  cipher,
                                  secret,
                                  secret_len,
                                  1);
}

static int ai_quic_tls_add_handshake_data_cb(SSL *ssl,
                                             enum ssl_encryption_level_t level,
                                             const uint8_t *data,
                                             size_t len) {
  ai_quic_tls_session_t *session;

  session = (ai_quic_tls_session_t *)SSL_get_app_data(ssl);
  if (session == NULL) {
    return 0;
  }
  return ai_quic_tls_queue_crypto(session, ai_quic_tls_from_ssl_level(level), data, len) ==
                 AI_QUIC_OK
             ? 1
             : 0;
}

static int ai_quic_tls_flush_flight_cb(SSL *ssl) {
  (void)ssl;
  return 1;
}

static int ai_quic_tls_send_alert_cb(SSL *ssl,
                                     enum ssl_encryption_level_t level,
                                     uint8_t alert) {
  ai_quic_tls_session_t *session;

  session = (ai_quic_tls_session_t *)SSL_get_app_data(ssl);
  if (session != NULL) {
    ai_quic_tls_set_error(session,
                          "tls alert level=%d alert=%u",
                          (int)level,
                          (unsigned int)alert);
  }
  return 1;
}

static const SSL_QUIC_METHOD kAiQuicMethod = {
    ai_quic_tls_set_read_secret_cb,
    ai_quic_tls_set_write_secret_cb,
    ai_quic_tls_add_handshake_data_cb,
    ai_quic_tls_flush_flight_cb,
    ai_quic_tls_send_alert_cb};

static ai_quic_result_t ai_quic_tls_boringssl_load_peer_params(
    ai_quic_tls_session_t *session) {
  const uint8_t *params;
  size_t params_len;

  if (session == NULL || session->mode != AI_QUIC_TLS_MODE_BORINGSSL_QUIC ||
      session->backend.boring.ssl == NULL) {
    return AI_QUIC_ERROR;
  }

  SSL_get_peer_quic_transport_params(session->backend.boring.ssl, &params, &params_len);
  if (params == NULL || params_len == 0u) {
    return AI_QUIC_OK;
  }
  if (ai_quic_transport_params_decode(params, params_len, &session->peer_params) !=
      AI_QUIC_OK) {
    ai_quic_tls_set_error(session, "%s", "peer transport parameters decode failed");
    return AI_QUIC_ERROR;
  }
  session->backend.boring.peer_params_loaded = 1;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_tls_boringssl_drive(ai_quic_tls_session_t *session) {
  int rc;
  int ssl_error;

  if (session == NULL || session->mode != AI_QUIC_TLS_MODE_BORINGSSL_QUIC ||
      session->backend.boring.ssl == NULL) {
    return AI_QUIC_ERROR;
  }

  if (session->handshake_complete) {
    if (SSL_process_quic_post_handshake(session->backend.boring.ssl) != 1) {
      ai_quic_tls_set_error(session, "%s", "process post-handshake failed");
      return AI_QUIC_ERROR;
    }
    return ai_quic_tls_boringssl_load_peer_params(session);
  }

  rc = SSL_do_handshake(session->backend.boring.ssl);
  if (rc != 1) {
    ssl_error = SSL_get_error(session->backend.boring.ssl, rc);
    if (ssl_error != SSL_ERROR_WANT_READ) {
      ai_quic_tls_set_error(session,
                            "SSL_do_handshake failed ssl_error=%d",
                            ssl_error);
      return AI_QUIC_ERROR;
    }
  }

  if (ai_quic_tls_boringssl_load_peer_params(session) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (!session->handshake_complete &&
      SSL_is_init_finished(session->backend.boring.ssl)) {
    session->handshake_complete = 1;
    if (!session->backend.boring.handshake_complete_reported &&
        ai_quic_tls_emit_install_event(session,
                                       AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE) ==
            AI_QUIC_OK) {
      session->backend.boring.handshake_complete_reported = 1;
    }
  }

  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_tls_boringssl_setup(
    ai_quic_tls_session_t *session) {
  SSL_CTX *ssl_ctx;
  SSL *ssl;
  char cert_path[1024];
  char key_path[1024];
  char ca_path[1024];
  uint8_t alpn_wire[64];
  size_t alpn_len;

  ssl_ctx = SSL_CTX_new(TLS_method());
  if (ssl_ctx == NULL) {
    return AI_QUIC_ERROR;
  }

  if (SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_quic_method(ssl_ctx, &kAiQuicMethod) != 1) {
    SSL_CTX_free(ssl_ctx);
    return AI_QUIC_ERROR;
  }

  SSL_CTX_set_keylog_callback(ssl_ctx, ai_quic_tls_keylog_callback);
  SSL_CTX_set_custom_verify(ssl_ctx, SSL_VERIFY_NONE, ai_quic_tls_verify_ok);

  if (session->is_server) {
    ai_quic_tls_join_path(cert_path, sizeof(cert_path), session->ctx->cert_root, "cert.pem");
    ai_quic_tls_join_path(key_path, sizeof(key_path), session->ctx->cert_root, "priv.key");
    if (!ai_quic_tls_file_exists(cert_path) || !ai_quic_tls_file_exists(key_path) ||
        SSL_CTX_use_certificate_chain_file(ssl_ctx, cert_path) != 1 ||
        SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ssl_ctx) != 1) {
      SSL_CTX_free(ssl_ctx);
      return AI_QUIC_ERROR;
    }
    SSL_CTX_set_alpn_select_cb(ssl_ctx, ai_quic_tls_alpn_select_cb, session);
  } else {
    ai_quic_tls_join_path(ca_path, sizeof(ca_path), session->ctx->cert_root, "ca.pem");
    if (ai_quic_tls_file_exists(ca_path)) {
      (void)SSL_CTX_load_verify_locations(ssl_ctx, ca_path, NULL);
    }
  }

  ssl = SSL_new(ssl_ctx);
  if (ssl == NULL) {
    SSL_CTX_free(ssl_ctx);
    return AI_QUIC_ERROR;
  }

  SSL_set_app_data(ssl, session);
  if (session->is_server) {
    SSL_set_accept_state(ssl);
  } else {
    SSL_set_connect_state(ssl);
    alpn_len = strlen(session->alpn);
    if (alpn_len > 0u) {
      if (alpn_len > 255u || alpn_len + 1u > sizeof(alpn_wire)) {
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        return AI_QUIC_ERROR;
      }
      alpn_wire[0] = (uint8_t)alpn_len;
      memcpy(alpn_wire + 1u, session->alpn, alpn_len);
      if (SSL_set_alpn_protos(ssl, alpn_wire, alpn_len + 1u) != 0) {
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        return AI_QUIC_ERROR;
      }
    }
  }

  session->backend.boring.ssl_ctx = ssl_ctx;
  session->backend.boring.ssl = ssl;
  return AI_QUIC_OK;
}

static int ai_quic_tls_should_use_boringssl(const ai_quic_tls_ctx_t *tls_ctx,
                                            int is_server) {
  char cert_path[1024];
  char key_path[1024];
  char ca_path[1024];

  if (tls_ctx == NULL || tls_ctx->cert_root[0] == '\0') {
    return 0;
  }

  ai_quic_tls_join_path(cert_path, sizeof(cert_path), tls_ctx->cert_root, "cert.pem");
  ai_quic_tls_join_path(key_path, sizeof(key_path), tls_ctx->cert_root, "priv.key");
  ai_quic_tls_join_path(ca_path, sizeof(ca_path), tls_ctx->cert_root, "ca.pem");
  return is_server ? (ai_quic_tls_file_exists(cert_path) &&
                      ai_quic_tls_file_exists(key_path))
                   : ai_quic_tls_file_exists(ca_path);
}

ai_quic_tls_ctx_t *ai_quic_tls_ctx_create(const char *cert_root) {
  ai_quic_tls_ctx_t *tls_ctx;

  tls_ctx = (ai_quic_tls_ctx_t *)calloc(1u, sizeof(*tls_ctx));
  if (tls_ctx == NULL) {
    return NULL;
  }
  if (cert_root != NULL) {
    snprintf(tls_ctx->cert_root, sizeof(tls_ctx->cert_root), "%s", cert_root);
  }
  return tls_ctx;
}

void ai_quic_tls_ctx_destroy(ai_quic_tls_ctx_t *tls_ctx) {
  if (tls_ctx == NULL) {
    return;
  }
  free(tls_ctx);
}

ai_quic_result_t ai_quic_tls_self_check(void) {
  ai_quic_tls_ctx_t *tls_ctx;

  tls_ctx = ai_quic_tls_ctx_create(NULL);
  if (tls_ctx == NULL) {
    return AI_QUIC_ERROR;
  }

  ai_quic_tls_ctx_destroy(tls_ctx);
  return AI_QUIC_OK;
}

const char *ai_quic_tls_backend_name(void) {
  return "BoringSSL";
}

ai_quic_tls_session_t *ai_quic_tls_session_create(ai_quic_tls_ctx_t *tls_ctx,
                                                  int is_server,
                                                  const char *alpn,
                                                  const char *keylog_path) {
  ai_quic_tls_session_t *session;

  if (tls_ctx == NULL) {
    return NULL;
  }

  session = (ai_quic_tls_session_t *)calloc(1u, sizeof(*session));
  if (session == NULL) {
    return NULL;
  }

  session->ctx = tls_ctx;
  session->is_server = is_server;
  session->mode = ai_quic_tls_should_use_boringssl(tls_ctx, is_server)
                      ? AI_QUIC_TLS_MODE_BORINGSSL_QUIC
                      : AI_QUIC_TLS_MODE_FAKE;
  if (alpn != NULL) {
    snprintf(session->alpn, sizeof(session->alpn), "%s", alpn);
  } else {
    snprintf(session->alpn, sizeof(session->alpn), "%s", "hq-interop");
  }

  if (keylog_path != NULL && keylog_path[0] != '\0') {
    session->keylog_file = fopen(keylog_path, "a");
  }

  if (session->mode == AI_QUIC_TLS_MODE_BORINGSSL_QUIC &&
      ai_quic_tls_boringssl_setup(session) != AI_QUIC_OK) {
    ai_quic_tls_session_destroy(session);
    return NULL;
  }

  if (session->mode == AI_QUIC_TLS_MODE_FAKE) {
    session->backend.fake.state =
        is_server ? AI_QUIC_TLS_STATE_WAIT_CLIENT_HELLO
                  : AI_QUIC_TLS_STATE_WAIT_SERVER_HELLO;
  }

  return session;
}

void ai_quic_tls_session_destroy(ai_quic_tls_session_t *session) {
  if (session == NULL) {
    return;
  }
  if (session->mode == AI_QUIC_TLS_MODE_BORINGSSL_QUIC) {
    if (session->backend.boring.ssl != NULL) {
      SSL_free(session->backend.boring.ssl);
    }
    if (session->backend.boring.ssl_ctx != NULL) {
      SSL_CTX_free(session->backend.boring.ssl_ctx);
    }
  }
  if (session->keylog_file != NULL) {
    fclose(session->keylog_file);
  }
  free(session);
}

ai_quic_result_t ai_quic_tls_session_start(
    ai_quic_tls_session_t *session,
    const ai_quic_transport_params_t *local_params,
    const char *server_name) {
  uint8_t message[AI_QUIC_MAX_FRAME_PAYLOAD_LEN];
  size_t tp_len;

  if (session == NULL || local_params == NULL) {
    return AI_QUIC_ERROR;
  }

  session->local_params = *local_params;
  if (server_name != NULL) {
    snprintf(session->server_name, sizeof(session->server_name), "%s", server_name);
  } else {
    session->server_name[0] = '\0';
  }
  if (ai_quic_transport_params_encode(local_params,
                                      session->local_params_wire,
                                      sizeof(session->local_params_wire),
                                      &session->local_params_wire_len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (session->mode == AI_QUIC_TLS_MODE_BORINGSSL_QUIC) {
    if (!session->is_server && session->server_name[0] != '\0' &&
        SSL_set_tlsext_host_name(session->backend.boring.ssl,
                                 session->server_name) != 1) {
      return AI_QUIC_ERROR;
    }
    if (SSL_set_quic_transport_params(session->backend.boring.ssl,
                                      session->local_params_wire,
                                      session->local_params_wire_len) != 1) {
      return AI_QUIC_ERROR;
    }
    return session->is_server ? AI_QUIC_OK
                              : ai_quic_tls_boringssl_drive(session);
  }

  if (session->is_server) {
    return AI_QUIC_OK;
  }

  message[0] = 1u;
  if (session->local_params_wire_len > sizeof(message) - 1u) {
    return AI_QUIC_ERROR;
  }
  memcpy(message + 1u, session->local_params_wire, session->local_params_wire_len);
  tp_len = session->local_params_wire_len;
  return ai_quic_tls_queue_crypto(session,
                                  AI_QUIC_ENCRYPTION_INITIAL,
                                  message,
                                  tp_len + 1u);
}

ai_quic_result_t ai_quic_tls_session_provide_crypto(
    ai_quic_tls_session_t *session,
    ai_quic_encryption_level_t level,
    const uint8_t *data,
    size_t data_len) {
  uint8_t message[AI_QUIC_MAX_FRAME_PAYLOAD_LEN];
  size_t tp_len;
  size_t events_before;

  if (session == NULL || data == NULL || data_len == 0u) {
    return AI_QUIC_ERROR;
  }

  if (session->mode == AI_QUIC_TLS_MODE_BORINGSSL_QUIC) {
    events_before = session->event_len;
    if (SSL_provide_quic_data(session->backend.boring.ssl,
                              ai_quic_tls_to_ssl_level(level),
                              data,
                              data_len) != 1) {
      return AI_QUIC_ERROR;
    }
    if (ai_quic_tls_boringssl_drive(session) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    if (!session->is_server) {
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "tls",
                        "provided crypto level=%d len=%zu events_before=%zu events_after=%zu handshake_complete=%d",
                        (int)level,
                        data_len,
                        events_before,
                        session->event_len,
                        session->handshake_complete);
    }
    return AI_QUIC_OK;
  }

  if (session->is_server) {
    if (session->backend.fake.state == AI_QUIC_TLS_STATE_WAIT_CLIENT_HELLO &&
        level == AI_QUIC_ENCRYPTION_INITIAL && data[0] == 1u) {
      if (ai_quic_transport_params_decode(data + 1u,
                                          data_len - 1u,
                                          &session->peer_params) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }

      if (ai_quic_tls_emit_install_event(session,
                                         AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS) !=
          AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }

      message[0] = 2u;
      if (session->local_params_wire_len > sizeof(message) - 1u) {
        return AI_QUIC_ERROR;
      }
      memcpy(message + 1u, session->local_params_wire, session->local_params_wire_len);
      tp_len = session->local_params_wire_len;
      session->backend.fake.state = AI_QUIC_TLS_STATE_WAIT_CLIENT_FINISHED;
      return ai_quic_tls_queue_crypto(session,
                                      AI_QUIC_ENCRYPTION_HANDSHAKE,
                                      message,
                                      tp_len + 1u);
    }

    if (session->backend.fake.state != AI_QUIC_TLS_STATE_WAIT_CLIENT_FINISHED ||
        level != AI_QUIC_ENCRYPTION_HANDSHAKE || data[0] != 3u) {
      return AI_QUIC_ERROR;
    }

    if (ai_quic_tls_emit_install_event(session,
                                       AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS) !=
        AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    session->handshake_complete = 1;
    session->backend.fake.state = AI_QUIC_TLS_STATE_COMPLETE;
    return ai_quic_tls_emit_install_event(session,
                                          AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE);
  }

  if (session->backend.fake.state != AI_QUIC_TLS_STATE_WAIT_SERVER_HELLO ||
      level != AI_QUIC_ENCRYPTION_HANDSHAKE || data[0] != 2u) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_transport_params_decode(data + 1u,
                                      data_len - 1u,
                                      &session->peer_params) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_tls_emit_install_event(session,
                                     AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS) !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  message[0] = 3u;
  if (ai_quic_tls_queue_crypto(session,
                               AI_QUIC_ENCRYPTION_HANDSHAKE,
                               message,
                               1u) != AI_QUIC_OK ||
      ai_quic_tls_emit_install_event(session,
                                     AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS) !=
          AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  session->handshake_complete = 1;
  session->backend.fake.state = AI_QUIC_TLS_STATE_COMPLETE;
  return ai_quic_tls_emit_install_event(session,
                                        AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE);
}

ai_quic_result_t ai_quic_tls_session_poll_event(ai_quic_tls_session_t *session,
                                                ai_quic_tls_event_t *event) {
  if (session == NULL || event == NULL) {
    return AI_QUIC_ERROR;
  }

  if (session->event_len == 0u) {
    ai_quic_tls_event_clear(event);
    return AI_QUIC_ERROR;
  }

  *event = session->events[session->event_head];
  session->event_head = (session->event_head + 1u) % AI_QUIC_TLS_MAX_EVENTS;
  session->event_len -= 1u;
  return AI_QUIC_OK;
}

const ai_quic_transport_params_t *ai_quic_tls_session_peer_transport_params(
    const ai_quic_tls_session_t *session) {
  if (session == NULL) {
    return NULL;
  }
  return &session->peer_params;
}

int ai_quic_tls_session_handshake_complete(
    const ai_quic_tls_session_t *session) {
  if (session == NULL) {
    return 0;
  }
  return session->handshake_complete;
}

ai_quic_result_t ai_quic_tls_session_get_packet_secret(
    const ai_quic_tls_session_t *session,
    ai_quic_encryption_level_t level,
    int is_write,
    uint32_t *cipher_suite,
    uint8_t *secret,
    size_t *secret_len,
    size_t secret_capacity) {
  static const char *kFakeLabels[2][2] = {
      {"CLIENT_HANDSHAKE_TRAFFIC_SECRET", "SERVER_HANDSHAKE_TRAFFIC_SECRET"},
      {"CLIENT_TRAFFIC_SECRET_0", "SERVER_TRAFFIC_SECRET_0"}};
  const ai_quic_tls_level_secret_t *slot;
  const char *label;
  size_t derived_len;
  int level_index;
  int role_index;

  if (session == NULL || cipher_suite == NULL || secret == NULL || secret_len == NULL ||
      secret_capacity == 0u || level == AI_QUIC_ENCRYPTION_INITIAL) {
    return AI_QUIC_ERROR;
  }

  if (session->mode == AI_QUIC_TLS_MODE_BORINGSSL_QUIC) {
    int ssl_level = ai_quic_tls_to_ssl_level(level);

    slot = is_write ? &session->backend.boring.write_levels[ssl_level]
                    : &session->backend.boring.read_levels[ssl_level];
    if (!slot->valid || slot->secret_len == 0u || slot->secret_len > secret_capacity) {
      return AI_QUIC_ERROR;
    }
    *cipher_suite = slot->cipher_suite;
    *secret_len = slot->secret_len;
    memcpy(secret, slot->secret, slot->secret_len);
    return AI_QUIC_OK;
  }

  level_index = level == AI_QUIC_ENCRYPTION_HANDSHAKE ? 0 : 1;
  role_index = (session->is_server == is_write) ? 1 : 0;
  label = kFakeLabels[level_index][role_index];
  derived_len = secret_capacity < 32u ? secret_capacity : 32u;
  if (ai_quic_tls_generate_initial_secret((const uint8_t *)label,
                                          strlen(label),
                                          secret,
                                          derived_len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  *cipher_suite = TLS1_CK_AES_128_GCM_SHA256;
  *secret_len = derived_len;
  return AI_QUIC_OK;
}
