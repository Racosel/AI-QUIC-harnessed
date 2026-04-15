#ifndef AI_QUIC_FRAME_H
#define AI_QUIC_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define AI_QUIC_MAX_FRAME_PAYLOAD_LEN 1500u
#define AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN 1200u
#define AI_QUIC_MAX_ACK_RANGES 8u

typedef enum ai_quic_frame_type {
  AI_QUIC_FRAME_PADDING = 0x00,
  AI_QUIC_FRAME_PING = 0x01,
  AI_QUIC_FRAME_ACK = 0x02,
  AI_QUIC_FRAME_CRYPTO = 0x06,
  AI_QUIC_FRAME_NEW_TOKEN = 0x07,
  AI_QUIC_FRAME_MAX_DATA = 0x10,
  AI_QUIC_FRAME_MAX_STREAM_DATA = 0x11,
  AI_QUIC_FRAME_DATA_BLOCKED = 0x14,
  AI_QUIC_FRAME_STREAM_DATA_BLOCKED = 0x15,
  AI_QUIC_FRAME_STREAM = 0x0f,
  AI_QUIC_FRAME_CONNECTION_CLOSE = 0x1c,
  AI_QUIC_FRAME_NEW_CONNECTION_ID = 0x18,
  AI_QUIC_FRAME_HANDSHAKE_DONE = 0x1e
} ai_quic_frame_type_t;

typedef struct ai_quic_ack_range {
  uint64_t gap;
  uint64_t ack_range;
} ai_quic_ack_range_t;

typedef struct ai_quic_ack_frame {
  uint64_t largest_acked;
  uint64_t first_ack_range;
  size_t ack_range_count;
  ai_quic_ack_range_t ack_ranges[AI_QUIC_MAX_ACK_RANGES];
} ai_quic_ack_frame_t;

typedef struct ai_quic_crypto_frame {
  uint64_t offset;
  size_t data_len;
  uint8_t data[AI_QUIC_MAX_FRAME_PAYLOAD_LEN];
} ai_quic_crypto_frame_t;

typedef struct ai_quic_max_data_frame {
  uint64_t maximum_data;
} ai_quic_max_data_frame_t;

typedef struct ai_quic_max_stream_data_frame {
  uint64_t stream_id;
  uint64_t maximum_stream_data;
} ai_quic_max_stream_data_frame_t;

typedef struct ai_quic_data_blocked_frame {
  uint64_t limit;
} ai_quic_data_blocked_frame_t;

typedef struct ai_quic_stream_data_blocked_frame {
  uint64_t stream_id;
  uint64_t limit;
} ai_quic_stream_data_blocked_frame_t;

typedef struct ai_quic_stream_frame {
  uint64_t stream_id;
  uint64_t offset;
  int fin;
  size_t data_len;
  uint8_t data[AI_QUIC_MAX_FRAME_PAYLOAD_LEN];
} ai_quic_stream_frame_t;

typedef struct ai_quic_connection_close_frame {
  uint64_t error_code;
  uint64_t frame_type;
  char reason[256];
} ai_quic_connection_close_frame_t;

typedef struct ai_quic_frame {
  ai_quic_frame_type_t type;
  union {
    ai_quic_ack_frame_t ack;
    ai_quic_crypto_frame_t crypto;
    ai_quic_max_data_frame_t max_data;
    ai_quic_max_stream_data_frame_t max_stream_data;
    ai_quic_data_blocked_frame_t data_blocked;
    ai_quic_stream_data_blocked_frame_t stream_data_blocked;
    ai_quic_stream_frame_t stream;
    ai_quic_connection_close_frame_t connection_close;
  } payload;
} ai_quic_frame_t;

#endif
