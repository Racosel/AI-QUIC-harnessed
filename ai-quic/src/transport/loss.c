#include "transport_internal.h"

void ai_quic_loss_on_packet_sent(ai_quic_conn_impl_t *conn,
                                 ai_quic_packet_number_space_id_t space_id,
                                 uint64_t packet_number,
                                 uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;

  if (conn == NULL) {
    return;
  }

  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }

  conn->loss_state[space_id].latest_sent_packet = packet_number;
  space->bytes_in_flight += 1u;
  if (space_id != AI_QUIC_PN_SPACE_APP_DATA || conn->handshake_confirmed) {
    space->pto_deadline_ms = now_ms + 100u;
  } else {
    space->pto_deadline_ms = 0u;
  }
}

void ai_quic_loss_discard_space(ai_quic_conn_impl_t *conn,
                                ai_quic_packet_number_space_id_t space_id) {
  ai_quic_packet_number_space_t *space;

  if (conn == NULL) {
    return;
  }
  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }
  ai_quic_pn_space_mark_key_discarded(space);
}
