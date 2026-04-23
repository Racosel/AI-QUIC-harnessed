#include "test_support.h"

#include <stdlib.h>

#include "ai_quic/fs.h"
#include "transport_internal.h"

int ai_quic_fake_link_init(ai_quic_fake_link_t *link,
                           ai_quic_endpoint_t *client,
                           ai_quic_endpoint_t *server) {
  if (link == NULL || client == NULL || server == NULL) {
    return 1;
  }
  link->client = client;
  link->server = server;
  link->now_ms = 1u;
  return 0;
}

static int ai_quic_fake_link_step(ai_quic_endpoint_t *from,
                                  ai_quic_endpoint_t *to,
                                  uint64_t now_ms) {
  static const uint8_t kClientAddr[] = "client-addr";
  static const uint8_t kServerAddr[] = "server-addr";
  ai_quic_endpoint_impl_t *from_impl;
  ai_quic_endpoint_impl_t *to_impl;
  uint8_t buffer[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  const uint8_t *peer_addr;
  size_t peer_addr_len;

  from_impl = (ai_quic_endpoint_impl_t *)from;
  to_impl = (ai_quic_endpoint_impl_t *)to;
  if (!ai_quic_endpoint_has_pending_datagrams(from)) {
    return 0;
  }

  if (from_impl != NULL &&
      from_impl->config.role == AI_QUIC_ENDPOINT_ROLE_CLIENT) {
    peer_addr = kClientAddr;
    peer_addr_len = sizeof(kClientAddr) - 1u;
  } else {
    peer_addr = kServerAddr;
    peer_addr_len = sizeof(kServerAddr) - 1u;
  }

  if (ai_quic_endpoint_pop_datagram(from, buffer, sizeof(buffer), &written) !=
          AI_QUIC_OK ||
      ai_quic_endpoint_receive_datagram_from(
          to, buffer, written, peer_addr, peer_addr_len, now_ms) != AI_QUIC_OK) {
    fprintf(stderr,
            "fake_link_step failed: from=%s to=%s from_pending=%zu to_pending=%zu written=%zu\n",
            ai_quic_endpoint_error(from),
            ai_quic_endpoint_error(to),
            from_impl != NULL ? from_impl->pending_len : 0u,
            to_impl != NULL ? to_impl->pending_len : 0u,
            written);
    return 1;
  }
  return 0;
}

static void ai_quic_dump_conn_state(const char *label, ai_quic_endpoint_t *endpoint) {
  ai_quic_endpoint_impl_t *impl;
  size_t i;

  impl = (ai_quic_endpoint_impl_t *)endpoint;
  if (impl == NULL || impl->conn == NULL) {
    return;
  }

  fprintf(stderr,
          "%s conn_flow: recv_limit=%llu recv_consumed=%llu highest_received=%llu send_limit=%llu update_pending=%d blocked_pending=%d total_requests=%zu completed=%zu\n",
          label,
          (unsigned long long)impl->conn->conn_flow.recv_limit,
          (unsigned long long)impl->conn->conn_flow.recv_consumed,
          (unsigned long long)impl->conn->conn_flow.highest_received,
          (unsigned long long)impl->conn->conn_flow.send_limit,
          impl->conn->conn_flow.update_pending,
          impl->conn->conn_flow.blocked_pending,
          impl->conn->total_request_streams,
          impl->conn->completed_request_streams);

  for (i = 0; i < AI_QUIC_ARRAY_LEN(impl->conn->streams.streams); ++i) {
    const ai_quic_stream_state_t *stream = &impl->conn->streams.streams[i];
    if (!stream->in_use) {
      continue;
    }
    fprintf(stderr,
            "%s stream[%zu]: id=%llu local=%d send=%llu/%zu recv=%llu highest_recv=%llu consumed=%llu written=%llu final_known=%d final=%llu recv_fin=%d send_fin_req=%d send_fin_sent=%d req_parsed=%d resp_prepared=%d resp_finished=%d download_finished=%d stream_send_limit=%llu stream_recv_limit=%llu update_pending=%d blocked_pending=%d\n",
            label,
            i,
            (unsigned long long)stream->stream_id,
            stream->is_local,
            (unsigned long long)stream->send_offset,
            stream->send_data_len,
            (unsigned long long)stream->recv_contiguous_end,
            (unsigned long long)stream->flow.highest_received,
            (unsigned long long)stream->app_consumed,
            (unsigned long long)stream->file_written_offset,
            stream->final_size_known,
            (unsigned long long)stream->final_size,
            stream->recv_fin,
            stream->send_fin_requested,
            stream->send_fin_sent,
            stream->request_parsed,
            stream->response_prepared,
            stream->response_finished,
            stream->download_finished,
            (unsigned long long)stream->flow.send_limit,
            (unsigned long long)stream->flow.recv_limit,
            stream->flow.update_pending,
            stream->flow.blocked_pending);
  }
}

int ai_quic_fake_link_pump(ai_quic_fake_link_t *link) {
  ai_quic_conn_info_t client_info;
  ai_quic_conn_info_t server_info;
  size_t rounds;

  if (link == NULL) {
    return 1;
  }

  for (rounds = 0; rounds < 32768u; ++rounds) {
    ai_quic_endpoint_impl_t *client_impl = (ai_quic_endpoint_impl_t *)link->client;
    ai_quic_endpoint_impl_t *server_impl = (ai_quic_endpoint_impl_t *)link->server;
    size_t client_pending = client_impl != NULL ? client_impl->pending_len : 0u;
    size_t server_pending = server_impl != NULL ? server_impl->pending_len : 0u;
    ai_quic_endpoint_t *from;
    ai_quic_endpoint_t *to;

    if (client_pending > 512u || server_pending > 512u) {
      if (client_pending > server_pending) {
        from = link->client;
        to = link->server;
      } else {
        from = link->server;
        to = link->client;
      }
    } else if (server_pending > 0u) {
      from = link->server;
      to = link->client;
    } else if (client_pending > 0u) {
      from = link->client;
      to = link->server;
    } else {
      from = link->client;
      to = link->server;
    }

    if (ai_quic_fake_link_step(from, to, link->now_ms++) != 0) {
      return 1;
    }
    if (ai_quic_endpoint_connection_info(link->client, &client_info) == AI_QUIC_OK &&
        client_info.handshake_confirmed && client_info.state == AI_QUIC_CONN_STATE_ACTIVE &&
        client_info.total_request_streams > 0u &&
        client_info.completed_request_streams == client_info.total_request_streams &&
        !ai_quic_endpoint_has_pending_datagrams(link->client) &&
        !ai_quic_endpoint_has_pending_datagrams(link->server)) {
      return 0;
    }
  }

  if (ai_quic_endpoint_connection_info(link->client, &client_info) == AI_QUIC_OK) {
    fprintf(stderr,
            "fake_link timeout: client state=%d handshake_completed=%d confirmed=%d can_send_1rtt=%d bytes_in=%llu bytes_out=%llu pending_client=%d pending_server=%d\n",
            (int)client_info.state,
            client_info.handshake_completed,
            client_info.handshake_confirmed,
            client_info.can_send_1rtt,
            (unsigned long long)client_info.bytes_received,
            (unsigned long long)client_info.bytes_sent,
            ai_quic_endpoint_has_pending_datagrams(link->client),
            ai_quic_endpoint_has_pending_datagrams(link->server));
  }
  if (ai_quic_endpoint_connection_info(link->server, &server_info) == AI_QUIC_OK) {
    fprintf(stderr,
            "fake_link timeout: server state=%d handshake_completed=%d confirmed=%d can_send_1rtt=%d bytes_in=%llu bytes_out=%llu\n",
            (int)server_info.state,
            server_info.handshake_completed,
            server_info.handshake_confirmed,
            server_info.can_send_1rtt,
            (unsigned long long)server_info.bytes_received,
            (unsigned long long)server_info.bytes_sent);
  }
  ai_quic_dump_conn_state("client", link->client);
  ai_quic_dump_conn_state("server", link->server);
  return 1;
}

int ai_quic_write_fixture_file(const char *root,
                               const char *name,
                               const uint8_t *data,
                               size_t data_len) {
  char path[512];

  if (root == NULL || name == NULL) {
    return 1;
  }
  snprintf(path, sizeof(path), "%s/%s", root, name);
  return ai_quic_fs_write_binary_file(path, data, data_len) == AI_QUIC_OK ? 0 : 1;
}

int ai_quic_read_fixture_file(const char *path,
                              uint8_t *buffer,
                              size_t capacity,
                              size_t *read_len) {
  uint8_t *data;
  size_t data_len;

  if (ai_quic_fs_read_binary_file(path, &data, &data_len) != AI_QUIC_OK ||
      capacity < data_len) {
    return 1;
  }
  memcpy(buffer, data, data_len);
  *read_len = data_len;
  free(data);
  return 0;
}
