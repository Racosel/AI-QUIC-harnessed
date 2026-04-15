#ifndef AI_QUIC_PN_SPACE_H
#define AI_QUIC_PN_SPACE_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/packet.h"

typedef enum ai_quic_packet_number_space_id {
  AI_QUIC_PN_SPACE_INITIAL = 0,
  AI_QUIC_PN_SPACE_HANDSHAKE = 1,
  AI_QUIC_PN_SPACE_APP_DATA = 2,
  AI_QUIC_PN_SPACE_COUNT = 3
} ai_quic_packet_number_space_id_t;

typedef enum ai_quic_key_state {
  AI_QUIC_KEY_STATE_NONE = 0,
  AI_QUIC_KEY_STATE_INSTALLED = 1,
  AI_QUIC_KEY_STATE_DISCARDED = 2
} ai_quic_key_state_t;

typedef struct ai_quic_crypto_stream_state {
  uint64_t rx_next_offset;
  uint64_t tx_next_offset;
} ai_quic_crypto_stream_state_t;

typedef struct ai_quic_packet_number_space {
  ai_quic_packet_number_space_id_t id;
  uint64_t next_packet_number;
  uint64_t largest_acked;
  uint64_t bytes_in_flight;
  uint64_t pto_deadline_ms;
  int ack_needed;
  ai_quic_key_state_t key_state;
  ai_quic_crypto_stream_state_t crypto_stream;
} ai_quic_packet_number_space_t;

#endif
