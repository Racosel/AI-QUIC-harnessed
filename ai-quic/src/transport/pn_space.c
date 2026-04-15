#include "transport_internal.h"

#include <string.h>

static void ai_quic_pn_space_merge_ranges(ai_quic_packet_number_space_t *space) {
  size_t index;

  if (space == NULL) {
    return;
  }

  index = 0u;
  while (index + 1u < space->received_range_count) {
    ai_quic_packet_range_t *high = &space->received_ranges[index];
    ai_quic_packet_range_t *low = &space->received_ranges[index + 1u];
    size_t tail;

    if (low->end + 1u < high->start) {
      index += 1u;
      continue;
    }

    if (low->start < high->start) {
      high->start = low->start;
    }
    if (low->end > high->end) {
      high->end = low->end;
    }
    for (tail = index + 1u; tail + 1u < space->received_range_count; ++tail) {
      space->received_ranges[tail] = space->received_ranges[tail + 1u];
    }
    space->received_range_count -= 1u;
  }
}

ai_quic_result_t ai_quic_pn_space_init(ai_quic_packet_number_space_t *space,
                                       ai_quic_packet_number_space_id_t id) {
  if (space == NULL) {
    return AI_QUIC_ERROR;
  }

  memset(space, 0, sizeof(*space));
  space->id = id;
  space->largest_received_packet_number = UINT64_MAX;
  space->largest_acked_by_peer = UINT64_MAX;
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
  space->received_range_count = 0u;
  space->largest_received_packet_number = UINT64_MAX;
}

void ai_quic_pn_space_on_packet_received(ai_quic_packet_number_space_t *space,
                                         uint64_t packet_number) {
  size_t index;
  size_t insert_at;

  if (space == NULL) {
    return;
  }
  if (space->largest_received_packet_number == UINT64_MAX ||
      packet_number > space->largest_received_packet_number) {
    space->largest_received_packet_number = packet_number;
  }

  for (index = 0u; index < space->received_range_count; ++index) {
    if (packet_number >= space->received_ranges[index].start &&
        packet_number <= space->received_ranges[index].end) {
      return;
    }
  }

  if (space->received_range_count < AI_QUIC_ARRAY_LEN(space->received_ranges)) {
    insert_at = space->received_range_count;
    space->received_range_count += 1u;
  } else {
    if (packet_number <= space->received_ranges[space->received_range_count - 1u].start) {
      return;
    }
    insert_at = space->received_range_count - 1u;
  }

  space->received_ranges[insert_at].start = packet_number;
  space->received_ranges[insert_at].end = packet_number;
  while (insert_at > 0u &&
         space->received_ranges[insert_at].start >
             space->received_ranges[insert_at - 1u].start) {
    ai_quic_packet_range_t tmp = space->received_ranges[insert_at];
    space->received_ranges[insert_at] = space->received_ranges[insert_at - 1u];
    space->received_ranges[insert_at - 1u] = tmp;
    insert_at -= 1u;
  }
  ai_quic_pn_space_merge_ranges(space);
}

void ai_quic_pn_space_on_ack(ai_quic_packet_number_space_t *space,
                             uint64_t largest_acked) {
  if (space == NULL) {
    return;
  }
  if (space->largest_acked_by_peer == UINT64_MAX ||
      largest_acked > space->largest_acked_by_peer) {
    space->largest_acked_by_peer = largest_acked;
  }
  space->ack_needed = 0;
  space->bytes_in_flight = 0u;
}
