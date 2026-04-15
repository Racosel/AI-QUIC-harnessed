#include "transport_internal.h"

#include <string.h>

void ai_quic_stream_reset(ai_quic_stream_state_t *stream, uint64_t stream_id) {
  if (stream == NULL) {
    return;
  }
  memset(stream, 0, sizeof(*stream));
  stream->stream_id = stream_id;
}

ai_quic_result_t ai_quic_stream_on_receive(ai_quic_stream_state_t *stream,
                                           const ai_quic_stream_frame_t *frame) {
  ai_quic_stream_frame_t incoming;
  uint64_t frame_end;
  uint64_t overlap;

  if (stream == NULL || frame == NULL) {
    return AI_QUIC_ERROR;
  }

  incoming = *frame;
  frame_end = incoming.offset + incoming.data_len;
  if (frame_end < incoming.offset) {
    return AI_QUIC_ERROR;
  }

  if (frame_end <= stream->recv_offset) {
    stream->recv_fin = stream->recv_fin || incoming.fin;
    return AI_QUIC_OK;
  }

  if (incoming.offset > stream->recv_offset) {
    return AI_QUIC_ERROR;
  }

  if (incoming.offset < stream->recv_offset) {
    overlap = stream->recv_offset - incoming.offset;
    if (overlap > incoming.data_len) {
      return AI_QUIC_ERROR;
    }
    if (incoming.data_len > (size_t)overlap) {
      memmove(incoming.data,
              incoming.data + overlap,
              incoming.data_len - (size_t)overlap);
    }
    incoming.offset = stream->recv_offset;
    incoming.data_len -= (size_t)overlap;
  }

  if (stream->recv_data_len + incoming.data_len > sizeof(stream->recv_data)) {
    return AI_QUIC_ERROR;
  }

  memcpy(stream->recv_data + stream->recv_data_len, incoming.data, incoming.data_len);
  stream->recv_data_len += incoming.data_len;
  stream->recv_offset += incoming.data_len;
  stream->recv_fin = stream->recv_fin || incoming.fin;
  return AI_QUIC_OK;
}
