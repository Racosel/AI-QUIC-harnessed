#include <stdio.h>

#include "ai_quic/conn.h"
#include "ai_quic/dispatcher.h"
#include "ai_quic/result.h"
#include "ai_quic/tls.h"
#include "ai_quic/version.h"

int main(void) {
  ai_quic_dispatcher_t *dispatcher;
  ai_quic_conn_t *conn;
  ai_quic_version_t versions[2];
  size_t version_count;

  dispatcher = ai_quic_dispatcher_create();
  if (dispatcher == NULL) {
    fprintf(stderr, "dispatcher allocation failed\n");
    return 1;
  }

  version_count =
      ai_quic_dispatcher_supported_versions(dispatcher, versions, 2u);
  if (version_count == 0u || versions[0] != AI_QUIC_VERSION_V1) {
    fprintf(stderr, "dispatcher supported version list is invalid\n");
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  conn = ai_quic_conn_create(AI_QUIC_VERSION_V1);
  if (conn == NULL) {
    fprintf(stderr, "connection allocation failed\n");
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  if (ai_quic_conn_version(conn) != AI_QUIC_VERSION_V1) {
    fprintf(stderr, "connection version mismatch\n");
    ai_quic_conn_destroy(conn);
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  if (ai_quic_conn_next_packet_number(conn, AI_QUIC_PN_SPACE_INITIAL) != 0u ||
      ai_quic_conn_next_packet_number(conn, AI_QUIC_PN_SPACE_HANDSHAKE) != 0u ||
      ai_quic_conn_next_packet_number(conn, AI_QUIC_PN_SPACE_APP_DATA) != 0u) {
    fprintf(stderr, "packet number spaces not initialized to zero\n");
    ai_quic_conn_destroy(conn);
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  if (ai_quic_tls_self_check() != AI_QUIC_OK) {
    fprintf(stderr, "tls self-check failed\n");
    ai_quic_conn_destroy(conn);
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  ai_quic_conn_destroy(conn);
  ai_quic_dispatcher_destroy(dispatcher);
  puts("ai_quic_smoke_test: ok");
  return 0;
}
