#include "common_internal.h"

#include <string.h>

size_t ai_quic_varint_size(uint64_t value) {
  if (value < (1ull << 6)) {
    return 1u;
  }
  if (value < (1ull << 14)) {
    return 2u;
  }
  if (value < (1ull << 30)) {
    return 4u;
  }
  return 8u;
}

ai_quic_result_t ai_quic_varint_write(uint8_t *buffer,
                                      size_t capacity,
                                      size_t *written,
                                      uint64_t value) {
  size_t len;
  size_t i;
  uint64_t encoded;

  if (buffer == NULL || written == NULL) {
    return AI_QUIC_ERROR;
  }

  len = ai_quic_varint_size(value);
  if (capacity < len) {
    return AI_QUIC_ERROR;
  }

  encoded = value;
  if (len == 2u) {
    encoded |= 1ull << 14;
  } else if (len == 4u) {
    encoded |= 2ull << 30;
  } else if (len == 8u) {
    encoded |= 3ull << 62;
  }

  for (i = 0; i < len; ++i) {
    buffer[len - 1u - i] = (uint8_t)(encoded & 0xffu);
    encoded >>= 8u;
  }

  *written = len;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_varint_read(const uint8_t *buffer,
                                     size_t buffer_len,
                                     size_t *consumed,
                                     uint64_t *value) {
  size_t len;
  size_t i;
  uint64_t decoded;

  if (buffer == NULL || consumed == NULL || value == NULL || buffer_len == 0u) {
    return AI_QUIC_ERROR;
  }

  len = 1u << (buffer[0] >> 6);
  if (buffer_len < len) {
    return AI_QUIC_ERROR;
  }

  decoded = (uint64_t)(buffer[0] & 0x3fu);
  for (i = 1; i < len; ++i) {
    decoded = (decoded << 8u) | buffer[i];
  }

  *consumed = len;
  *value = decoded;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_write_u16(uint8_t *buffer,
                                   size_t capacity,
                                   size_t *written,
                                   uint16_t value) {
  if (buffer == NULL || written == NULL || capacity < 2u) {
    return AI_QUIC_ERROR;
  }
  buffer[0] = (uint8_t)(value >> 8u);
  buffer[1] = (uint8_t)(value & 0xffu);
  *written = 2u;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_write_u32(uint8_t *buffer,
                                   size_t capacity,
                                   size_t *written,
                                   uint32_t value) {
  if (buffer == NULL || written == NULL || capacity < 4u) {
    return AI_QUIC_ERROR;
  }
  buffer[0] = (uint8_t)(value >> 24u);
  buffer[1] = (uint8_t)(value >> 16u);
  buffer[2] = (uint8_t)(value >> 8u);
  buffer[3] = (uint8_t)(value & 0xffu);
  *written = 4u;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_write_u64(uint8_t *buffer,
                                   size_t capacity,
                                   size_t *written,
                                   uint64_t value) {
  size_t i;

  if (buffer == NULL || written == NULL || capacity < 8u) {
    return AI_QUIC_ERROR;
  }

  for (i = 0; i < 8u; ++i) {
    buffer[i] = (uint8_t)(value >> (56u - (8u * i)));
  }

  *written = 8u;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_read_u16(const uint8_t *buffer,
                                  size_t buffer_len,
                                  size_t *consumed,
                                  uint16_t *value) {
  if (buffer == NULL || consumed == NULL || value == NULL || buffer_len < 2u) {
    return AI_QUIC_ERROR;
  }
  *value = (uint16_t)(((uint16_t)buffer[0] << 8u) | buffer[1]);
  *consumed = 2u;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_read_u32(const uint8_t *buffer,
                                  size_t buffer_len,
                                  size_t *consumed,
                                  uint32_t *value) {
  if (buffer == NULL || consumed == NULL || value == NULL || buffer_len < 4u) {
    return AI_QUIC_ERROR;
  }
  *value = ((uint32_t)buffer[0] << 24u) | ((uint32_t)buffer[1] << 16u) |
           ((uint32_t)buffer[2] << 8u) | buffer[3];
  *consumed = 4u;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_read_u64(const uint8_t *buffer,
                                  size_t buffer_len,
                                  size_t *consumed,
                                  uint64_t *value) {
  size_t i;
  uint64_t decoded;

  if (buffer == NULL || consumed == NULL || value == NULL || buffer_len < 8u) {
    return AI_QUIC_ERROR;
  }

  decoded = 0u;
  for (i = 0; i < 8u; ++i) {
    decoded = (decoded << 8u) | buffer[i];
  }

  *consumed = 8u;
  *value = decoded;
  return AI_QUIC_OK;
}
