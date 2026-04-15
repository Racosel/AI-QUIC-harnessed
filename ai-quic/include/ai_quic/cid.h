#ifndef AI_QUIC_CID_H
#define AI_QUIC_CID_H

#include <stddef.h>
#include <stdint.h>

#define AI_QUIC_MAX_CID_LEN 20u

typedef struct ai_quic_cid {
  size_t len;
  uint8_t bytes[AI_QUIC_MAX_CID_LEN];
} ai_quic_cid_t;

int ai_quic_cid_equal(const ai_quic_cid_t *lhs, const ai_quic_cid_t *rhs);
void ai_quic_cid_clear(ai_quic_cid_t *cid);
int ai_quic_cid_from_bytes(ai_quic_cid_t *cid,
                           const uint8_t *bytes,
                           size_t len);
int ai_quic_cid_copy(ai_quic_cid_t *dst, const ai_quic_cid_t *src);

#endif
