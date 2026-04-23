#include "transport_internal.h"

#include <string.h>

static const ai_quic_version_ops_t kAiQuicVersions[] = {
    {AI_QUIC_VERSION_V1,
     "v1",
     0x00u,
     0x01u,
     0x02u,
     0x03u,
     {0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
      0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a},
     {0xd9, 0xc9, 0x94, 0x3e, 0x61, 0x01, 0xfd, 0x20,
      0x00, 0x21, 0x50, 0x6b, 0xcc, 0x02, 0x81, 0x4c,
      0x73, 0x03, 0x0f, 0x25, 0xc7, 0x9d, 0x71, 0xce,
      0x87, 0x6e, 0xca, 0x87, 0x6e, 0x6f, 0xca, 0x8e},
     {0xbe, 0x0c, 0x69, 0x0b, 0x9f, 0x66, 0x57, 0x5a,
      0x1d, 0x76, 0x6b, 0x54, 0xe3, 0x68, 0xc8, 0x4e},
     {0x46, 0x15, 0x99, 0xd3, 0x5d, 0x63,
      0x2b, 0xf2, 0x23, 0x98, 0x25, 0xbb},
     "quic key",
     "quic iv",
     "quic hp",
     "quic ku"},
    {AI_QUIC_VERSION_V2,
     "v2",
     0x01u,
     0x02u,
     0x03u,
     0x00u,
     {0x0d, 0xed, 0xe3, 0xde, 0xf7, 0x00, 0xa6, 0xdb, 0x81, 0x93,
      0x81, 0xbe, 0x6e, 0x26, 0x9d, 0xcb, 0xf9, 0xbd, 0x2e, 0xd9},
     {0xc4, 0xdd, 0x24, 0x84, 0xd6, 0x81, 0xae, 0xfa,
      0x4f, 0xf4, 0xd6, 0x9c, 0x2c, 0x20, 0x29, 0x99,
      0x84, 0xa7, 0x65, 0xa5, 0xd3, 0xc3, 0x19, 0x82,
      0xf3, 0x8f, 0xc7, 0x41, 0x62, 0x15, 0x5e, 0x9f},
     {0x8f, 0xb4, 0xb0, 0x1b, 0x56, 0xac, 0x48, 0xe2,
      0x60, 0xfb, 0xcb, 0xce, 0xad, 0x7c, 0xcc, 0x92},
     {0xd8, 0x69, 0x69, 0xbc, 0x2d, 0x7c,
      0x6d, 0x99, 0x90, 0xef, 0xb0, 0x4a},
     "quicv2 key",
     "quicv2 iv",
     "quicv2 hp",
     "quicv2 ku"}};

const ai_quic_version_ops_t *ai_quic_version_ops_find(ai_quic_version_t version) {
  size_t i;

  for (i = 0u; i < AI_QUIC_ARRAY_LEN(kAiQuicVersions); ++i) {
    if (kAiQuicVersions[i].wire_version == version) {
      return &kAiQuicVersions[i];
    }
  }
  return NULL;
}

size_t ai_quic_version_supported_list(ai_quic_version_t *versions, size_t capacity) {
  size_t i;
  size_t copy_len;

  copy_len = AI_QUIC_ARRAY_LEN(kAiQuicVersions);
  if (versions == NULL || capacity == 0u) {
    return copy_len;
  }
  if (copy_len > capacity) {
    copy_len = capacity;
  }
  for (i = 0u; i < copy_len; ++i) {
    versions[i] = kAiQuicVersions[i].wire_version;
  }
  return AI_QUIC_ARRAY_LEN(kAiQuicVersions);
}

size_t ai_quic_version_offered_list(ai_quic_version_t *versions, size_t capacity) {
  return ai_quic_version_supported_list(versions, capacity);
}

size_t ai_quic_version_fully_deployed_list(ai_quic_version_t *versions,
                                           size_t capacity) {
  return ai_quic_version_supported_list(versions, capacity);
}

int ai_quic_version_supported(ai_quic_version_t version) {
  return ai_quic_version_ops_find(version) != NULL;
}

int ai_quic_version_reserved(ai_quic_version_t version) {
  return (version & 0x0f0f0f0fu) == 0x0a0a0a0au;
}

int ai_quic_version_compatible(ai_quic_version_t from, ai_quic_version_t to) {
  if (from == 0u || to == 0u || ai_quic_version_reserved(from) ||
      ai_quic_version_reserved(to)) {
    return 0;
  }
  if (from == to) {
    return ai_quic_version_supported(from);
  }
  return (from == AI_QUIC_VERSION_V1 && to == AI_QUIC_VERSION_V2) ||
         (from == AI_QUIC_VERSION_V2 && to == AI_QUIC_VERSION_V1);
}

int ai_quic_version_information_contains(
    const ai_quic_version_information_t *version_information,
    ai_quic_version_t version) {
  size_t i;

  if (version_information == NULL || version == 0u) {
    return 0;
  }
  for (i = 0u; i < version_information->available_versions_len; ++i) {
    if (version_information->available_versions[i] == version) {
      return 1;
    }
  }
  return 0;
}

ai_quic_packet_type_t ai_quic_version_decode_long_header_type(
    ai_quic_version_t version,
    uint8_t first_byte) {
  const ai_quic_version_ops_t *ops;
  uint8_t bits;

  ops = ai_quic_version_ops_find(version);
  if (ops == NULL) {
    return AI_QUIC_PACKET_TYPE_INITIAL;
  }

  bits = (uint8_t)((first_byte >> 4u) & 0x03u);
  if (bits == ops->initial_type_bits) {
    return AI_QUIC_PACKET_TYPE_INITIAL;
  }
  if (bits == ops->handshake_type_bits) {
    return AI_QUIC_PACKET_TYPE_HANDSHAKE;
  }
  return AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION;
}

ai_quic_result_t ai_quic_version_encode_long_header_first_byte(
    ai_quic_version_t version,
    ai_quic_packet_type_t type,
    uint8_t pn_len,
    uint8_t *first_byte) {
  const ai_quic_version_ops_t *ops;
  uint8_t type_bits;

  if (first_byte == NULL || pn_len == 0u || pn_len > 4u) {
    return AI_QUIC_ERROR;
  }

  ops = ai_quic_version_ops_find(version);
  if (ops == NULL) {
    return AI_QUIC_ERROR;
  }

  if (type == AI_QUIC_PACKET_TYPE_INITIAL) {
    type_bits = ops->initial_type_bits;
  } else if (type == AI_QUIC_PACKET_TYPE_HANDSHAKE) {
    type_bits = ops->handshake_type_bits;
  } else {
    return AI_QUIC_ERROR;
  }

  *first_byte = (uint8_t)(0xc0u | (uint8_t)(type_bits << 4u) | ((pn_len - 1u) & 0x03u));
  return AI_QUIC_OK;
}
