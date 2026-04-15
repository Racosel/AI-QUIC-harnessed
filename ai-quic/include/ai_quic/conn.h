#ifndef AI_QUIC_CONN_H
#define AI_QUIC_CONN_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/cid.h"
#include "ai_quic/pn_space.h"
#include "ai_quic/result.h"
#include "ai_quic/transport_params.h"
#include "ai_quic/version.h"

typedef enum ai_quic_conn_state {
  AI_QUIC_CONN_STATE_PRE_VALIDATION = 0,
  AI_QUIC_CONN_STATE_HANDSHAKING = 1,
  AI_QUIC_CONN_STATE_ACTIVE = 2,
  AI_QUIC_CONN_STATE_CLOSING = 3,
  AI_QUIC_CONN_STATE_CLOSED = 4
} ai_quic_conn_state_t;

typedef struct ai_quic_conn ai_quic_conn_t;

typedef struct ai_quic_conn_info {
  ai_quic_version_t version;
  ai_quic_conn_state_t state;
  ai_quic_cid_t local_cid;
  ai_quic_cid_t peer_cid;
  ai_quic_cid_t original_destination_cid;
  ai_quic_transport_params_t local_transport_params;
  ai_quic_transport_params_t peer_transport_params;
  int handshake_completed;
  int can_send_1rtt;
  int handshake_confirmed;
  int address_validated;
  uint64_t bytes_received;
  uint64_t bytes_sent;
  size_t total_request_streams;
  size_t completed_request_streams;
} ai_quic_conn_info_t;

ai_quic_conn_t *ai_quic_conn_create(ai_quic_version_t version, int is_server);
void ai_quic_conn_destroy(ai_quic_conn_t *conn);
ai_quic_version_t ai_quic_conn_version(const ai_quic_conn_t *conn);
ai_quic_conn_state_t ai_quic_conn_state(const ai_quic_conn_t *conn);
uint64_t ai_quic_conn_next_packet_number(const ai_quic_conn_t *conn,
                                         ai_quic_packet_number_space_id_t space_id);
const ai_quic_packet_number_space_t *ai_quic_conn_packet_number_space(
    const ai_quic_conn_t *conn,
    ai_quic_packet_number_space_id_t space_id);
int ai_quic_conn_handshake_completed(const ai_quic_conn_t *conn);
int ai_quic_conn_can_send_1rtt(const ai_quic_conn_t *conn);
int ai_quic_conn_handshake_confirmed(const ai_quic_conn_t *conn);
int ai_quic_conn_address_validated(const ai_quic_conn_t *conn);
ai_quic_result_t ai_quic_conn_get_info(const ai_quic_conn_t *conn,
                                       ai_quic_conn_info_t *info);

#endif
