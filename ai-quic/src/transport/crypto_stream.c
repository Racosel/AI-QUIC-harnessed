#include "transport_internal.h"

#include <string.h>

ai_quic_result_t ai_quic_crypto_stream_accept(ai_quic_packet_number_space_t *space,
                                              ai_quic_crypto_frame_t *frame) {
  uint64_t frame_end;
  uint64_t overlap;

  if (space == NULL || frame == NULL) {
    return AI_QUIC_ERROR;
  }

  frame_end = frame->offset + frame->data_len;
  if (frame_end < frame->offset) {
    return AI_QUIC_ERROR;
  }

  if (frame_end <= space->crypto_stream.rx_next_offset) {
    frame->offset = space->crypto_stream.rx_next_offset;
    frame->data_len = 0u;
    return AI_QUIC_OK;
  }

  if (frame->offset > space->crypto_stream.rx_next_offset) {
    return AI_QUIC_ERROR;
  }

  if (frame->offset < space->crypto_stream.rx_next_offset) {
    overlap = space->crypto_stream.rx_next_offset - frame->offset;
    if (overlap > frame->data_len) {
      return AI_QUIC_ERROR;
    }
    memmove(frame->data, frame->data + overlap, frame->data_len - (size_t)overlap);
    frame->offset = space->crypto_stream.rx_next_offset;
    frame->data_len -= (size_t)overlap;
  }

  space->crypto_stream.rx_next_offset += frame->data_len;
  return AI_QUIC_OK;
}
