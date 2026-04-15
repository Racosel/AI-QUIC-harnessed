#ifndef AI_QUIC_STREAM_H
#define AI_QUIC_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define AI_QUIC_HTTP09_STREAM_ID 0u
#define AI_QUIC_MAX_STREAM_BUFFER_LEN 8192u

typedef struct ai_quic_stream_state {
  uint64_t stream_id;
  uint64_t send_offset;
  uint64_t recv_offset;
  int recv_fin;
  int send_fin;
  size_t recv_data_len;
  uint8_t recv_data[AI_QUIC_MAX_STREAM_BUFFER_LEN];
} ai_quic_stream_state_t;

#endif
