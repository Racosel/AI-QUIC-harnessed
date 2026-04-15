#include "common_internal.h"

#include <openssl/rand.h>

uint64_t ai_quic_now_ms(void);

ai_quic_result_t ai_quic_random_fill(uint8_t *buffer, size_t len) {
  if (buffer == NULL || (len > 0u && RAND_bytes(buffer, (int)len) != 1)) {
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_random_cid(ai_quic_cid_t *cid, size_t len) {
  if (cid == NULL || len == 0u || len > AI_QUIC_MAX_CID_LEN) {
    return AI_QUIC_ERROR;
  }
  ai_quic_cid_clear(cid);
  if (ai_quic_random_fill(cid->bytes, len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  cid->len = len;
  return AI_QUIC_OK;
}
