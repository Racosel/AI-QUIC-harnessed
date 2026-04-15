#ifndef AI_QUIC_PACKET_H
#define AI_QUIC_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/cid.h"
#include "ai_quic/frame.h"
#include "ai_quic/version.h"

#define AI_QUIC_MAX_TOKEN_LEN 256u
#define AI_QUIC_MAX_FRAMES_PER_PACKET 8u
#define AI_QUIC_MAX_SUPPORTED_VERSIONS 8u
#define AI_QUIC_MAX_PACKET_SIZE 4096u
#define AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE 1200u

typedef enum ai_quic_packet_type {
  AI_QUIC_PACKET_TYPE_INITIAL = 0,
  AI_QUIC_PACKET_TYPE_HANDSHAKE = 1,
  AI_QUIC_PACKET_TYPE_ONE_RTT = 2,
  AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION = 3
} ai_quic_packet_type_t;

typedef enum ai_quic_encryption_level {
  AI_QUIC_ENCRYPTION_INITIAL = 0,
  AI_QUIC_ENCRYPTION_HANDSHAKE = 1,
  AI_QUIC_ENCRYPTION_APPLICATION = 2
} ai_quic_encryption_level_t;

typedef struct ai_quic_packet_header {
  ai_quic_packet_type_t type;
  ai_quic_version_t version;
  ai_quic_cid_t dcid;
  ai_quic_cid_t scid;
  uint64_t packet_number;
  size_t token_len;
  uint8_t token[AI_QUIC_MAX_TOKEN_LEN];
  size_t payload_length;
  size_t packet_length;
} ai_quic_packet_header_t;

typedef struct ai_quic_packet {
  ai_quic_packet_header_t header;
  ai_quic_frame_t frames[AI_QUIC_MAX_FRAMES_PER_PACKET];
  size_t frame_count;
  ai_quic_version_t supported_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t supported_versions_len;
} ai_quic_packet_t;

#endif
