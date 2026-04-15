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

static int ai_quic_fake_link_drain(ai_quic_endpoint_t *from,
                                   ai_quic_endpoint_t *to,
                                   uint64_t now_ms) {
  uint8_t buffer[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;

  while (ai_quic_endpoint_has_pending_datagrams(from)) {
    if (ai_quic_endpoint_pop_datagram(from, buffer, sizeof(buffer), &written) !=
            AI_QUIC_OK ||
        ai_quic_endpoint_receive_datagram(to, buffer, written, now_ms) !=
            AI_QUIC_OK) {
      fprintf(stderr,
              "fake_link_drain failed: from=%s to=%s\n",
              ai_quic_endpoint_error(from),
              ai_quic_endpoint_error(to));
      return 1;
    }
  }
  return 0;
}

int ai_quic_fake_link_pump(ai_quic_fake_link_t *link) {
  ai_quic_conn_info_t client_info;
  ai_quic_conn_info_t server_info;
  size_t rounds;

  if (link == NULL) {
    return 1;
  }

  for (rounds = 0; rounds < 64u; ++rounds) {
    if (ai_quic_fake_link_drain(link->client, link->server, link->now_ms++) != 0 ||
        ai_quic_fake_link_drain(link->server, link->client, link->now_ms++) != 0) {
      return 1;
    }
    if (ai_quic_endpoint_connection_info(link->client, &client_info) == AI_QUIC_OK &&
        client_info.handshake_confirmed && client_info.state == AI_QUIC_CONN_STATE_ACTIVE &&
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
