#include "transport_internal.h"

#include <string.h>

static ai_quic_packet_type_t ai_quic_invariant_long_header_packet_type(
    uint8_t first_byte) {
  uint8_t bits;

  bits = (uint8_t)((first_byte >> 4u) & 0x03u);
  if (bits == 0u) {
    return AI_QUIC_PACKET_TYPE_INITIAL;
  }
  if (bits == 2u) {
    return AI_QUIC_PACKET_TYPE_HANDSHAKE;
  }
  return AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION;
}

ai_quic_result_t ai_quic_parse_invariant_header(const uint8_t *datagram,
                                                size_t datagram_len,
                                                ai_quic_packet_header_t *header) {
  size_t offset;
  size_t chunk;
  uint32_t version;

  if (datagram == NULL || header == NULL || datagram_len < 2u) {
    return AI_QUIC_ERROR;
  }

  memset(header, 0, sizeof(*header));
  offset = 0u;

  if ((datagram[0] & 0x80u) == 0u) {
    header->type = AI_QUIC_PACKET_TYPE_ONE_RTT;
    header->packet_length = datagram_len;
    return AI_QUIC_OK;
  }

  header->type = ai_quic_invariant_long_header_packet_type(datagram[0]);
  offset += 1u;
  if (ai_quic_read_u32(datagram + offset, datagram_len - offset, &chunk, &version) !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  header->version = version;
  offset += chunk;

  if (datagram_len < offset + 1u) {
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_cid_from_bytes(&header->dcid, datagram + offset + 1u, datagram[offset])) {
    return AI_QUIC_ERROR;
  }
  offset += 1u + datagram[offset];

  if (datagram_len < offset + 1u) {
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_cid_from_bytes(&header->scid, datagram + offset + 1u, datagram[offset])) {
    return AI_QUIC_ERROR;
  }

  if (header->version == 0u) {
    header->type = AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION;
  }
  header->packet_length = datagram_len;
  return AI_QUIC_OK;
}
