#include "transport_internal.h"

void ai_quic_build_ack_frame(const ai_quic_packet_number_space_t *space,
                             ai_quic_frame_t *frame) {
  size_t i;

  if (space == NULL || frame == NULL || space->received_range_count == 0u) {
    return;
  }

  if (frame == NULL) {
    return;
  }
  frame->type = AI_QUIC_FRAME_ACK;
  frame->payload.ack.largest_acked = space->received_ranges[0].end;
  frame->payload.ack.first_ack_range =
      space->received_ranges[0].end - space->received_ranges[0].start;
  frame->payload.ack.ack_range_count = 0u;

  for (i = 1u; i < space->received_range_count && i <= AI_QUIC_MAX_ACK_RANGES; ++i) {
    const ai_quic_packet_range_t *prev = &space->received_ranges[i - 1u];
    const ai_quic_packet_range_t *curr = &space->received_ranges[i];
    ai_quic_ack_range_t *range = &frame->payload.ack.ack_ranges[i - 1u];

    if (prev->start <= curr->end + 1u) {
      continue;
    }

    range->gap = prev->start - curr->end - 2u;
    range->ack_range = curr->end - curr->start;
    frame->payload.ack.ack_range_count += 1u;
  }
}
