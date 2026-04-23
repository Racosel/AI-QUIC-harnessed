#include "transport_internal.h"

#include <string.h>

ai_quic_result_t ai_quic_build_version_negotiation(
    const ai_quic_dispatcher_t *dispatcher,
    const ai_quic_packet_header_t *incoming,
    ai_quic_packet_t *packet) {
  if (dispatcher == NULL || incoming == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  memset(packet, 0, sizeof(*packet));
  packet->header.type = AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION;
  packet->header.version = 0u;
  if (!ai_quic_cid_copy(&packet->header.dcid, &incoming->scid) ||
      !ai_quic_cid_copy(&packet->header.scid, &incoming->dcid)) {
    return AI_QUIC_ERROR;
  }
  packet->supported_versions_len = ai_quic_dispatcher_offered_versions(
      dispatcher,
      packet->supported_versions,
      AI_QUIC_ARRAY_LEN(packet->supported_versions));
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_build_retry(const ai_quic_packet_header_t *incoming,
                                     const ai_quic_cid_t *retry_source_cid,
                                     const uint8_t *token,
                                     size_t token_len,
                                     ai_quic_packet_t *packet) {
  if (incoming == NULL || retry_source_cid == NULL || packet == NULL ||
      token == NULL || token_len == 0u || token_len > AI_QUIC_MAX_TOKEN_LEN) {
    return AI_QUIC_ERROR;
  }

  memset(packet, 0, sizeof(*packet));
  packet->header.type = AI_QUIC_PACKET_TYPE_RETRY;
  packet->header.version = incoming->version;
  packet->header.retry_original_dcid = incoming->dcid;
  if (!ai_quic_cid_copy(&packet->header.dcid, &incoming->scid) ||
      !ai_quic_cid_copy(&packet->header.scid, retry_source_cid)) {
    return AI_QUIC_ERROR;
  }
  memcpy(packet->header.token, token, token_len);
  packet->header.token_len = token_len;
  return AI_QUIC_OK;
}
