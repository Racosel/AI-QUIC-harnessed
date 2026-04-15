#include "ai_quic/endpoint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common_internal.h"
#include "transport_internal.h"

static const char *ai_quic_qlog_packet_type_name(ai_quic_packet_type_t type) {
  switch (type) {
    case AI_QUIC_PACKET_TYPE_INITIAL:
      return "initial";
    case AI_QUIC_PACKET_TYPE_HANDSHAKE:
      return "handshake";
    case AI_QUIC_PACKET_TYPE_ONE_RTT:
      return "1rtt";
    case AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION:
      return "version_negotiation";
    default:
      return "unknown";
  }
}

static void ai_quic_format_cid_hex(const ai_quic_cid_t *cid,
                                   char *buffer,
                                   size_t capacity) {
  static const char kHex[] = "0123456789abcdef";
  size_t i;
  size_t offset;

  if (buffer == NULL || capacity == 0u) {
    return;
  }

  if (cid == NULL) {
    buffer[0] = '\0';
    return;
  }

  offset = 0u;
  for (i = 0; i < cid->len && offset + 2u < capacity; ++i) {
    buffer[offset++] = kHex[(cid->bytes[i] >> 4u) & 0x0fu];
    buffer[offset++] = kHex[cid->bytes[i] & 0x0fu];
  }
  buffer[offset] = '\0';
}

static void ai_quic_format_preview_hex(const uint8_t *bytes,
                                       size_t len,
                                       char *buffer,
                                       size_t capacity) {
  static const char kHex[] = "0123456789abcdef";
  size_t preview_len;
  size_t i;
  size_t offset;

  if (buffer == NULL || capacity == 0u) {
    return;
  }

  buffer[0] = '\0';
  if (bytes == NULL || len == 0u) {
    return;
  }

  preview_len = len < 24u ? len : 24u;
  offset = 0u;
  for (i = 0; i < preview_len && offset + 2u < capacity; ++i) {
    buffer[offset++] = kHex[(bytes[i] >> 4u) & 0x0fu];
    buffer[offset++] = kHex[bytes[i] & 0x0fu];
  }
  if (preview_len < len && offset + 4u < capacity) {
    buffer[offset++] = '.';
    buffer[offset++] = '.';
    buffer[offset++] = '.';
  }
  buffer[offset] = '\0';
}

static void ai_quic_qlog_packet(ai_quic_qlog_writer_t *qlog,
                                uint64_t now_ms,
                                const char *event_name,
                                const ai_quic_packet_t *packet,
                                const uint8_t *raw_bytes,
                                size_t raw_len) {
  char dcid[AI_QUIC_MAX_CID_LEN * 2u + 1u];
  char scid[AI_QUIC_MAX_CID_LEN * 2u + 1u];
  char preview[64];
  char data_json[512];

  if (qlog == NULL || event_name == NULL || packet == NULL) {
    return;
  }

  ai_quic_format_cid_hex(&packet->header.dcid, dcid, sizeof(dcid));
  ai_quic_format_cid_hex(&packet->header.scid, scid, sizeof(scid));
  ai_quic_format_preview_hex(raw_bytes, raw_len, preview, sizeof(preview));
  snprintf(data_json,
           sizeof(data_json),
           "{\"header\":{\"packet_type\":\"%s\",\"packet_number\":%llu,"
           "\"dcid\":\"%s\",\"scid\":\"%s\",\"version\":%u},\"raw\":{\"length\":%zu},"
           "\"preview\":\"%s\",\"frames\":[{\"frame_count\":%zu}]}",
           ai_quic_qlog_packet_type_name(packet->header.type),
           (unsigned long long)packet->header.packet_number,
           dcid,
           scid,
           packet->header.version,
           raw_len,
           preview,
           packet->frame_count);
  ai_quic_qlog_write_event(qlog, now_ms, "transport", event_name, data_json);
}

static void ai_quic_qlog_drop(ai_quic_qlog_writer_t *qlog,
                              uint64_t now_ms,
                              const char *reason,
                              const ai_quic_packet_header_t *header,
                              size_t datagram_len,
                              uint8_t first_byte,
                              const uint8_t *raw_bytes,
                              size_t raw_len) {
  char dcid[AI_QUIC_MAX_CID_LEN * 2u + 1u];
  char scid[AI_QUIC_MAX_CID_LEN * 2u + 1u];
  char preview[64];
  char data_json[512];

  if (qlog == NULL || reason == NULL) {
    return;
  }

  dcid[0] = '\0';
  scid[0] = '\0';
  if (header != NULL) {
    ai_quic_format_cid_hex(&header->dcid, dcid, sizeof(dcid));
    ai_quic_format_cid_hex(&header->scid, scid, sizeof(scid));
  }
  ai_quic_format_preview_hex(raw_bytes, raw_len, preview, sizeof(preview));

  snprintf(data_json,
           sizeof(data_json),
           "{\"reason\":\"%s\",\"raw\":{\"length\":%zu,\"first_byte\":%u},"
           "\"preview\":\"%s\",\"header\":{\"packet_type\":\"%s\",\"version\":%u,"
           "\"dcid\":\"%s\",\"scid\":\"%s\"}}",
           reason,
           datagram_len,
           (unsigned int)first_byte,
           preview,
           header != NULL ? ai_quic_qlog_packet_type_name(header->type) : "unknown",
           header != NULL ? header->version : 0u,
           dcid,
           scid);
  ai_quic_qlog_write_event(qlog, now_ms, "transport", "packet_dropped", data_json);
}

void ai_quic_endpoint_config_init(ai_quic_endpoint_config_t *config,
                                  ai_quic_endpoint_role_t role) {
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  config->role = role;
  config->idle_timeout_ms = 30000u;
  config->local_cid_len = 8u;
  config->alpn = "hq-interop";
}

static void ai_quic_endpoint_push(ai_quic_endpoint_impl_t *endpoint,
                                  const ai_quic_pending_datagram_t *datagram) {
  size_t index;

  if (endpoint == NULL || datagram == NULL ||
      endpoint->pending_len >= AI_QUIC_MAX_PENDING_DATAGRAMS) {
    return;
  }
  index = (endpoint->pending_head + endpoint->pending_len) % AI_QUIC_MAX_PENDING_DATAGRAMS;
  endpoint->pending[index] = *datagram;
  endpoint->pending_len += 1u;
}

static int ai_quic_is_all_zero(const uint8_t *buffer, size_t len) {
  size_t i;

  if (buffer == NULL) {
    return 0;
  }
  for (i = 0; i < len; ++i) {
    if (buffer[i] != 0u) {
      return 0;
    }
  }
  return 1;
}

ai_quic_endpoint_t *ai_quic_endpoint_create(
    const ai_quic_endpoint_config_t *config) {
  ai_quic_endpoint_impl_t *endpoint;

  if (config == NULL) {
    return NULL;
  }

  endpoint = (ai_quic_endpoint_impl_t *)calloc(1u, sizeof(*endpoint));
  if (endpoint == NULL) {
    return NULL;
  }

  endpoint->config = *config;
  endpoint->status = AI_QUIC_OK;
  endpoint->dispatcher = ai_quic_dispatcher_create();
  endpoint->tls_ctx = ai_quic_tls_ctx_create(config->cert_root);
  if (endpoint->dispatcher == NULL || endpoint->tls_ctx == NULL) {
    ai_quic_endpoint_destroy((ai_quic_endpoint_t *)endpoint);
    return NULL;
  }

  endpoint->qlog = ai_quic_qlog_writer_create(
      config->qlog_path != NULL ? config->qlog_path : "/tmp/ai_quic_qlog.jsonl",
      "ai-quic-endpoint",
      config->role == AI_QUIC_ENDPOINT_ROLE_SERVER ? "server" : "client");
  return (ai_quic_endpoint_t *)endpoint;
}

void ai_quic_endpoint_destroy(ai_quic_endpoint_t *endpoint) {
  ai_quic_endpoint_impl_t *impl;

  impl = (ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL) {
    return;
  }

  ai_quic_conn_destroy((ai_quic_conn_t *)impl->conn);
  ai_quic_dispatcher_destroy(impl->dispatcher);
  ai_quic_tls_ctx_destroy(impl->tls_ctx);
  ai_quic_qlog_writer_destroy(impl->qlog);
  free(impl);
}

static ai_quic_result_t ai_quic_endpoint_drain_conn(ai_quic_endpoint_impl_t *endpoint,
                                                    uint64_t now_ms) {
  ai_quic_pending_datagram_t staged[AI_QUIC_MAX_PENDING_DATAGRAMS];
  size_t staged_len;
  size_t i;

  if (endpoint == NULL || endpoint->conn == NULL) {
    return AI_QUIC_OK;
  }

  staged_len = 0u;
  if (ai_quic_conn_flush_pending(endpoint->conn,
                                 now_ms,
                                 staged,
                                 &staged_len,
                                 AI_QUIC_ARRAY_LEN(staged),
                                 endpoint->config.www_root) != AI_QUIC_OK) {
    endpoint->status = AI_QUIC_ERROR;
    ai_quic_set_error(endpoint->error, sizeof(endpoint->error), "%s",
                      endpoint->conn->last_error);
    return AI_QUIC_ERROR;
  }

  for (i = 0; i < staged_len; ++i) {
    ai_quic_endpoint_push(endpoint, &staged[i]);
  }

  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_endpoint_start_client(ai_quic_endpoint_t *endpoint,
                                               const char *authority,
                                               const char *request_path) {
  ai_quic_endpoint_impl_t *impl;

  impl = (ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL || impl->config.role != AI_QUIC_ENDPOINT_ROLE_CLIENT) {
    return AI_QUIC_ERROR;
  }

  impl->conn = (ai_quic_conn_impl_t *)ai_quic_conn_create(AI_QUIC_VERSION_V1, 0);
  if (impl->conn == NULL) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_conn_init_transport(impl->conn,
                                  impl->tls_ctx,
                                  impl->qlog,
                                  impl->config.alpn,
                                  impl->config.keylog_path) != AI_QUIC_OK ||
      ai_quic_conn_start_client(impl->conn, authority, request_path) !=
          AI_QUIC_OK) {
    impl->status = AI_QUIC_ERROR;
    ai_quic_set_error(impl->error, sizeof(impl->error), "%s",
                      impl->conn->last_error);
    return AI_QUIC_ERROR;
  }

  if (authority != NULL) {
    ai_quic_set_error(impl->authority, sizeof(impl->authority), "%s", authority);
  }
  if (request_path != NULL) {
    ai_quic_set_error(impl->request_path, sizeof(impl->request_path), "%s", request_path);
  }

  return ai_quic_endpoint_drain_conn(impl, ai_quic_now_ms());
}

ai_quic_result_t ai_quic_endpoint_receive_datagram(ai_quic_endpoint_t *endpoint,
                                                   const uint8_t *datagram,
                                                   size_t datagram_len,
                                                   uint64_t now_ms) {
  ai_quic_endpoint_impl_t *impl;
  ai_quic_dispatch_decision_t decision;
  ai_quic_packet_t packet;
  size_t offset;
  size_t consumed;
  ai_quic_pending_datagram_t staged[AI_QUIC_MAX_PENDING_DATAGRAMS];
  size_t staged_len;
  size_t i;

  impl = (ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL || datagram == NULL || datagram_len == 0u) {
    return AI_QUIC_ERROR;
  }

  if (!ai_quic_dispatcher_route_datagram(impl->dispatcher,
                                         datagram,
                                         datagram_len,
                                         &decision)) {
    impl->status = AI_QUIC_ERROR;
    ai_quic_set_error(impl->error,
                      sizeof(impl->error),
                      "dispatcher route failed len=%zu first_byte=0x%02x",
                      datagram_len,
                      datagram[0]);
    ai_quic_qlog_drop(impl->qlog,
                      now_ms,
                      "dispatcher_route_failed",
                      NULL,
                      datagram_len,
                      datagram[0],
                      datagram,
                      datagram_len);
    return AI_QUIC_ERROR;
  }

  if (decision.action == AI_QUIC_DISPATCH_DROP) {
    ai_quic_qlog_drop(impl->qlog,
                      now_ms,
                      "dispatcher_drop",
                      &decision.header,
                      datagram_len,
                      datagram[0],
                      datagram,
                      datagram_len);
    return AI_QUIC_OK;
  }

  if (decision.action == AI_QUIC_DISPATCH_VERSION_NEGOTIATION) {
    ai_quic_packet_t vn;
    ai_quic_pending_datagram_t out;

    if (ai_quic_build_version_negotiation(impl->dispatcher,
                                          &decision.header,
                                          &vn) != AI_QUIC_OK ||
        ai_quic_packet_encode(&vn, out.bytes, sizeof(out.bytes), &out.len) !=
            AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    vn.header.packet_length = out.len;
    ai_quic_qlog_packet(impl->qlog, now_ms, "packet_sent", &vn, out.bytes, out.len);
    ai_quic_endpoint_push(impl, &out);
    return AI_QUIC_OK;
  }

  if (impl->conn == NULL && decision.action == AI_QUIC_DISPATCH_CREATE_CONN) {
    impl->conn = (ai_quic_conn_impl_t *)ai_quic_conn_create(AI_QUIC_VERSION_V1, 1);
    if (impl->conn == NULL ||
        ai_quic_conn_init_transport(impl->conn,
                                    impl->tls_ctx,
                                    impl->qlog,
                                    impl->config.alpn,
                                    impl->config.keylog_path) != AI_QUIC_OK) {
      impl->status = AI_QUIC_ERROR;
      return AI_QUIC_ERROR;
    }
  }

  if (impl->conn == NULL) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  staged_len = 0u;
  while (offset < datagram_len) {
    if (ai_quic_is_all_zero(datagram + offset, datagram_len - offset)) {
      break;
    }
    if (ai_quic_packet_decode_conn(impl->conn,
                                   datagram + offset,
                                   datagram_len - offset,
                                   &consumed,
                                   &packet) != AI_QUIC_OK) {
      ai_quic_packet_header_t invariant;

      impl->status = AI_QUIC_ERROR;
      if (ai_quic_parse_invariant_header(datagram + offset,
                                         datagram_len - offset,
                                         &invariant) == AI_QUIC_OK) {
        char preview[64];

        ai_quic_format_preview_hex(datagram + offset,
                                   datagram_len - offset,
                                   preview,
                                   sizeof(preview));
        ai_quic_set_error(impl->error,
                          sizeof(impl->error),
                          "packet decode failed type=%d version=0x%08x len=%zu preview=%s detail=%s",
                          (int)invariant.type,
                          invariant.version,
                          datagram_len - offset,
                          preview,
                          ai_quic_packet_decode_last_error());
        ai_quic_qlog_drop(impl->qlog,
                          now_ms,
                          "packet_decode_failed",
                          &invariant,
                          datagram_len - offset,
                          datagram[offset],
                          datagram + offset,
                          datagram_len - offset);
      } else {
        char preview[64];

        ai_quic_format_preview_hex(datagram + offset,
                                   datagram_len - offset,
                                   preview,
                                   sizeof(preview));
        ai_quic_set_error(impl->error,
                          sizeof(impl->error),
                          "packet decode failed len=%zu first_byte=0x%02x preview=%s detail=%s",
                          datagram_len - offset,
                          datagram[offset],
                          preview,
                          ai_quic_packet_decode_last_error());
        ai_quic_qlog_drop(impl->qlog,
                          now_ms,
                          "packet_decode_failed",
                          NULL,
                          datagram_len - offset,
                          datagram[offset],
                          datagram + offset,
                          datagram_len - offset);
      }
      return AI_QUIC_ERROR;
    }
    ai_quic_qlog_packet(impl->qlog,
                        now_ms,
                        "packet_received",
                        &packet,
                        datagram + offset,
                        consumed);
    if (ai_quic_conn_on_packet(impl->conn,
                               &packet,
                               now_ms,
                               staged,
                               &staged_len,
                               AI_QUIC_ARRAY_LEN(staged),
                               impl->config.www_root,
                               impl->config.downloads_root) != AI_QUIC_OK) {
      impl->status = AI_QUIC_ERROR;
      ai_quic_set_error(impl->error, sizeof(impl->error), "%s",
                        impl->conn->last_error);
      return AI_QUIC_ERROR;
    }
    offset += consumed;
  }

  for (i = 0; i < staged_len; ++i) {
    ai_quic_endpoint_push(impl, &staged[i]);
  }

  return ai_quic_endpoint_drain_conn(impl, now_ms);
}

ai_quic_result_t ai_quic_endpoint_on_timeout(ai_quic_endpoint_t *endpoint,
                                             uint64_t now_ms) {
  return ai_quic_endpoint_drain_conn((ai_quic_endpoint_impl_t *)endpoint, now_ms);
}

int ai_quic_endpoint_has_pending_datagrams(const ai_quic_endpoint_t *endpoint) {
  const ai_quic_endpoint_impl_t *impl;

  impl = (const ai_quic_endpoint_impl_t *)endpoint;
  return impl != NULL && impl->pending_len > 0u;
}

ai_quic_result_t ai_quic_endpoint_pop_datagram(ai_quic_endpoint_t *endpoint,
                                               uint8_t *buffer,
                                               size_t capacity,
                                               size_t *written) {
  ai_quic_endpoint_impl_t *impl;
  const ai_quic_pending_datagram_t *datagram;

  impl = (ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL || buffer == NULL || written == NULL || impl->pending_len == 0u) {
    return AI_QUIC_ERROR;
  }

  datagram = &impl->pending[impl->pending_head];
  if (capacity < datagram->len) {
    return AI_QUIC_ERROR;
  }
  memcpy(buffer, datagram->bytes, datagram->len);
  *written = datagram->len;
  impl->pending_head = (impl->pending_head + 1u) % AI_QUIC_MAX_PENDING_DATAGRAMS;
  impl->pending_len -= 1u;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_endpoint_connection_info(
    const ai_quic_endpoint_t *endpoint,
    ai_quic_conn_info_t *info) {
  const ai_quic_endpoint_impl_t *impl;

  impl = (const ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL || impl->conn == NULL) {
    return AI_QUIC_ERROR;
  }
  return ai_quic_conn_get_info((const ai_quic_conn_t *)impl->conn, info);
}

ai_quic_result_t ai_quic_endpoint_status(const ai_quic_endpoint_t *endpoint) {
  const ai_quic_endpoint_impl_t *impl;

  impl = (const ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL) {
    return AI_QUIC_ERROR;
  }
  return impl->status;
}

const char *ai_quic_endpoint_error(const ai_quic_endpoint_t *endpoint) {
  const ai_quic_endpoint_impl_t *impl;

  impl = (const ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL) {
    return "invalid endpoint";
  }
  return impl->error;
}
