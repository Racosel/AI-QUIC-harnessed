#ifndef AI_QUIC_COMMON_INTERNAL_H
#define AI_QUIC_COMMON_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/cid.h"
#include "ai_quic/result.h"

#define AI_QUIC_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

size_t ai_quic_varint_size(uint64_t value);
ai_quic_result_t ai_quic_varint_write(uint8_t *buffer,
                                      size_t capacity,
                                      size_t *written,
                                      uint64_t value);
ai_quic_result_t ai_quic_varint_read(const uint8_t *buffer,
                                     size_t buffer_len,
                                     size_t *consumed,
                                     uint64_t *value);

ai_quic_result_t ai_quic_write_u16(uint8_t *buffer,
                                   size_t capacity,
                                   size_t *written,
                                   uint16_t value);
ai_quic_result_t ai_quic_write_u32(uint8_t *buffer,
                                   size_t capacity,
                                   size_t *written,
                                   uint32_t value);
ai_quic_result_t ai_quic_write_u64(uint8_t *buffer,
                                   size_t capacity,
                                   size_t *written,
                                   uint64_t value);
ai_quic_result_t ai_quic_read_u16(const uint8_t *buffer,
                                  size_t buffer_len,
                                  size_t *consumed,
                                  uint16_t *value);
ai_quic_result_t ai_quic_read_u32(const uint8_t *buffer,
                                  size_t buffer_len,
                                  size_t *consumed,
                                  uint32_t *value);
ai_quic_result_t ai_quic_read_u64(const uint8_t *buffer,
                                  size_t buffer_len,
                                  size_t *consumed,
                                  uint64_t *value);

uint64_t ai_quic_now_ms(void);
ai_quic_result_t ai_quic_random_fill(uint8_t *buffer, size_t len);
ai_quic_result_t ai_quic_random_cid(ai_quic_cid_t *cid, size_t len);

int ai_quic_path_extract_filename(const char *path,
                                  char *buffer,
                                  size_t capacity);
int ai_quic_url_split(const char *url,
                      char *host,
                      size_t host_capacity,
                      uint16_t *port,
                      char *path,
                      size_t path_capacity);

#endif
