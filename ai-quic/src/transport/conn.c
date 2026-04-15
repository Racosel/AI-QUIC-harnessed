#include "ai_quic/conn.h"

#include <string.h>
#include <stdlib.h>

#include "transport_internal.h"

ai_quic_conn_t *ai_quic_conn_create(ai_quic_version_t version, int is_server) {
  ai_quic_conn_impl_t *conn;
  size_t i;

  conn = (ai_quic_conn_impl_t *)calloc(1u, sizeof(*conn));
  if (conn == NULL) {
    return NULL;
  }

  conn->version = version;
  conn->is_server = is_server;
  conn->state = AI_QUIC_CONN_STATE_PRE_VALIDATION;
  ai_quic_transport_params_init(&conn->local_transport_params);
  ai_quic_transport_params_init(&conn->peer_transport_params);
  ai_quic_stream_manager_init(&conn->streams);
  ai_quic_flow_controller_init(&conn->conn_flow, AI_QUIC_INITIAL_MAX_DATA);
  for (i = 0; i < AI_QUIC_PN_SPACE_COUNT; ++i) {
    ai_quic_pn_space_init(&conn->packet_spaces[i], (ai_quic_packet_number_space_id_t)i);
  }
  return conn;
}

void ai_quic_conn_destroy(ai_quic_conn_t *conn) {
  ai_quic_conn_impl_t *impl;

  impl = (ai_quic_conn_impl_t *)conn;
  if (impl == NULL) {
    return;
  }
  ai_quic_tls_session_destroy(impl->tls_session);
  ai_quic_stream_manager_cleanup(&impl->streams);
  free(impl);
}

ai_quic_version_t ai_quic_conn_version(const ai_quic_conn_t *conn) {
  if (conn == NULL) {
    return 0u;
  }

  return conn->version;
}

ai_quic_conn_state_t ai_quic_conn_state(const ai_quic_conn_t *conn) {
  if (conn == NULL) {
    return AI_QUIC_CONN_STATE_CLOSED;
  }
  return ((const ai_quic_conn_impl_t *)conn)->state;
}

uint64_t ai_quic_conn_next_packet_number(const ai_quic_conn_t *conn,
                                         ai_quic_packet_number_space_id_t space_id) {
  if (conn == NULL || space_id >= AI_QUIC_PN_SPACE_COUNT) {
    return 0u;
  }

  return ((const ai_quic_conn_impl_t *)conn)->packet_spaces[space_id].next_packet_number;
}

const ai_quic_packet_number_space_t *ai_quic_conn_packet_number_space(
    const ai_quic_conn_t *conn,
    ai_quic_packet_number_space_id_t space_id) {
  if (conn == NULL || space_id >= AI_QUIC_PN_SPACE_COUNT) {
    return NULL;
  }
  return &((const ai_quic_conn_impl_t *)conn)->packet_spaces[space_id];
}

int ai_quic_conn_handshake_completed(const ai_quic_conn_t *conn) {
  if (conn == NULL) {
    return 0;
  }
  return ((const ai_quic_conn_impl_t *)conn)->handshake_completed;
}

int ai_quic_conn_can_send_1rtt(const ai_quic_conn_t *conn) {
  if (conn == NULL) {
    return 0;
  }
  return ((const ai_quic_conn_impl_t *)conn)->can_send_1rtt;
}

int ai_quic_conn_handshake_confirmed(const ai_quic_conn_t *conn) {
  if (conn == NULL) {
    return 0;
  }
  return ((const ai_quic_conn_impl_t *)conn)->handshake_confirmed;
}

int ai_quic_conn_address_validated(const ai_quic_conn_t *conn) {
  if (conn == NULL) {
    return 0;
  }
  return ((const ai_quic_conn_impl_t *)conn)->address_validated;
}

ai_quic_result_t ai_quic_conn_get_info(const ai_quic_conn_t *conn,
                                       ai_quic_conn_info_t *info) {
  const ai_quic_conn_impl_t *impl;

  if (conn == NULL || info == NULL) {
    return AI_QUIC_ERROR;
  }

  impl = (const ai_quic_conn_impl_t *)conn;
  memset(info, 0, sizeof(*info));
  info->version = impl->version;
  info->state = impl->state;
  info->local_cid = impl->local_cid;
  info->peer_cid = impl->peer_cid;
  info->original_destination_cid = impl->original_destination_cid;
  info->local_transport_params = impl->local_transport_params;
  info->peer_transport_params = impl->peer_transport_params;
  info->handshake_completed = impl->handshake_completed;
  info->can_send_1rtt = impl->can_send_1rtt;
  info->handshake_confirmed = impl->handshake_confirmed;
  info->address_validated = impl->address_validated;
  info->bytes_received = impl->bytes_received;
  info->bytes_sent = impl->bytes_sent;
  info->total_request_streams = impl->total_request_streams;
  info->completed_request_streams = impl->completed_request_streams;
  return AI_QUIC_OK;
}
