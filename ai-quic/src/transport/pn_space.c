#include "transport_internal.h"

#include <string.h>

ai_quic_result_t ai_quic_pn_space_init(ai_quic_packet_number_space_t *space,
                                       ai_quic_packet_number_space_id_t id) {
  if (space == NULL) {
    return AI_QUIC_ERROR;
  }

  memset(space, 0, sizeof(*space));
  space->id = id;
  space->largest_acked = UINT64_MAX;
  return AI_QUIC_OK;
}

void ai_quic_pn_space_mark_key_installed(ai_quic_packet_number_space_t *space) {
  if (space == NULL) {
    return;
  }
  space->key_state = AI_QUIC_KEY_STATE_INSTALLED;
}

void ai_quic_pn_space_mark_key_discarded(ai_quic_packet_number_space_t *space) {
  if (space == NULL) {
    return;
  }
  space->key_state = AI_QUIC_KEY_STATE_DISCARDED;
  space->bytes_in_flight = 0u;
  space->pto_deadline_ms = 0u;
  space->ack_needed = 0;
}

void ai_quic_pn_space_on_packet_received(ai_quic_packet_number_space_t *space,
                                         uint64_t packet_number) {
  if (space == NULL) {
    return;
  }
  if (space->largest_acked == UINT64_MAX || packet_number > space->largest_acked) {
    space->largest_acked = packet_number;
  }
  space->ack_needed = 1;
}

void ai_quic_pn_space_on_ack(ai_quic_packet_number_space_t *space,
                             uint64_t largest_acked) {
  if (space == NULL) {
    return;
  }
  if (space->largest_acked == UINT64_MAX || largest_acked > space->largest_acked) {
    space->largest_acked = largest_acked;
  }
  space->ack_needed = 0;
  space->bytes_in_flight = 0u;
}
