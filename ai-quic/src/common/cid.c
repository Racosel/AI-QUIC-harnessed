#include "ai_quic/cid.h"

#include <string.h>

int ai_quic_cid_equal(const ai_quic_cid_t *lhs, const ai_quic_cid_t *rhs) {
  if (lhs == NULL || rhs == NULL) {
    return 0;
  }
  if (lhs->len != rhs->len) {
    return 0;
  }
  if (lhs->len == 0u) {
    return 1;
  }
  return memcmp(lhs->bytes, rhs->bytes, lhs->len) == 0;
}

void ai_quic_cid_clear(ai_quic_cid_t *cid) {
  if (cid == NULL) {
    return;
  }
  cid->len = 0u;
  memset(cid->bytes, 0, sizeof(cid->bytes));
}

int ai_quic_cid_from_bytes(ai_quic_cid_t *cid,
                           const uint8_t *bytes,
                           size_t len) {
  if (cid == NULL || bytes == NULL || len > AI_QUIC_MAX_CID_LEN) {
    return 0;
  }
  ai_quic_cid_clear(cid);
  memcpy(cid->bytes, bytes, len);
  cid->len = len;
  return 1;
}

int ai_quic_cid_copy(ai_quic_cid_t *dst, const ai_quic_cid_t *src) {
  if (dst == NULL || src == NULL) {
    return 0;
  }
  return ai_quic_cid_from_bytes(dst, src->bytes, src->len);
}
