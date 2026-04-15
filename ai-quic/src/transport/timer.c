#include "transport_internal.h"

void ai_quic_timer_on_space_active(ai_quic_conn_impl_t *conn,
                                   ai_quic_packet_number_space_id_t space_id,
                                   uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;

  if (conn == NULL) {
    return;
  }
  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }

  if (space_id == AI_QUIC_PN_SPACE_APP_DATA && !conn->handshake_confirmed) {
    space->pto_deadline_ms = 0u;
    return;
  }

  if (space->bytes_in_flight > 0u) {
    space->pto_deadline_ms = now_ms + 100u;
  }
}
