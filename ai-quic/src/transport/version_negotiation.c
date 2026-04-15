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
  packet->supported_versions_len = ai_quic_dispatcher_supported_versions(
      dispatcher,
      packet->supported_versions,
      AI_QUIC_ARRAY_LEN(packet->supported_versions));
  return AI_QUIC_OK;
}
