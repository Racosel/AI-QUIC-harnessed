#ifndef AI_QUIC_ENDPOINT_H
#define AI_QUIC_ENDPOINT_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/conn.h"
#include "ai_quic/result.h"
#include "ai_quic/tls.h"

typedef enum ai_quic_endpoint_role {
  AI_QUIC_ENDPOINT_ROLE_CLIENT = 0,
  AI_QUIC_ENDPOINT_ROLE_SERVER = 1
} ai_quic_endpoint_role_t;

typedef struct ai_quic_endpoint ai_quic_endpoint_t;

typedef struct ai_quic_endpoint_config {
  ai_quic_endpoint_role_t role;
  const char *log_path;
  const char *qlog_path;
  const char *keylog_path;
  const char *www_root;
  const char *downloads_root;
  const char *cert_root;
  const char *alpn;
  const char *testcase;
  ai_quic_tls_cipher_policy_t cipher_policy;
  uint64_t idle_timeout_ms;
  size_t local_cid_len;
} ai_quic_endpoint_config_t;

typedef struct ai_quic_datagram_view {
  const uint8_t *data;
  size_t len;
} ai_quic_datagram_view_t;

void ai_quic_endpoint_config_init(ai_quic_endpoint_config_t *config,
                                  ai_quic_endpoint_role_t role);
ai_quic_endpoint_t *ai_quic_endpoint_create(
    const ai_quic_endpoint_config_t *config);
void ai_quic_endpoint_destroy(ai_quic_endpoint_t *endpoint);

ai_quic_result_t ai_quic_endpoint_start_client(ai_quic_endpoint_t *endpoint,
                                               const char *authority,
                                               const char *request_path);
ai_quic_result_t ai_quic_endpoint_queue_request(ai_quic_endpoint_t *endpoint,
                                                const char *request_path);
ai_quic_result_t ai_quic_endpoint_receive_datagram(ai_quic_endpoint_t *endpoint,
                                                   const uint8_t *datagram,
                                                   size_t datagram_len,
                                                   uint64_t now_ms);
ai_quic_result_t ai_quic_endpoint_on_timeout(ai_quic_endpoint_t *endpoint,
                                             uint64_t now_ms);
int ai_quic_endpoint_has_pending_datagrams(const ai_quic_endpoint_t *endpoint);
ai_quic_result_t ai_quic_endpoint_pop_datagram(ai_quic_endpoint_t *endpoint,
                                               uint8_t *buffer,
                                               size_t capacity,
                                               size_t *written);
ai_quic_result_t ai_quic_endpoint_connection_info(
    const ai_quic_endpoint_t *endpoint,
    ai_quic_conn_info_t *info);
ai_quic_result_t ai_quic_endpoint_status(const ai_quic_endpoint_t *endpoint);
const char *ai_quic_endpoint_error(const ai_quic_endpoint_t *endpoint);

#endif
