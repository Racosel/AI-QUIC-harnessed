#include "transport_internal.h"

void ai_quic_build_ack_frame(ai_quic_frame_t *frame, uint64_t largest_acked) {
  if (frame == NULL) {
    return;
  }
  frame->type = AI_QUIC_FRAME_ACK;
  frame->payload.ack.largest_acked = largest_acked;
}
