#include "ai_quic/dispatcher.h"

#include <stdlib.h>
#include <string.h>

#include "ai_quic/log.h"
#include "transport_internal.h"

struct ai_quic_dispatcher {
  ai_quic_version_t acceptable_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t acceptable_versions_len;
  ai_quic_version_t offered_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t offered_versions_len;
  ai_quic_version_t fully_deployed_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t fully_deployed_versions_len;
  int retry_enabled;
  uint8_t retry_token_key[AI_QUIC_RETRY_TOKEN_KEY_LEN];
};

static int ai_quic_dispatcher_parse_initial_token(
    const ai_quic_packet_header_t *header,
    const uint8_t *datagram,
    size_t datagram_len,
    const uint8_t **token,
    size_t *token_len) {
  size_t offset;
  size_t chunk;
  uint64_t parsed_token_len;

  if (header == NULL || datagram == NULL || token == NULL || token_len == NULL ||
      header->type != AI_QUIC_PACKET_TYPE_INITIAL) {
    return 0;
  }

  offset = 1u + 4u + 1u + header->dcid.len + 1u + header->scid.len;
  if (datagram_len < offset) {
    return 0;
  }
  if (ai_quic_varint_read(datagram + offset,
                          datagram_len - offset,
                          &chunk,
                          &parsed_token_len) != AI_QUIC_OK ||
      parsed_token_len > AI_QUIC_MAX_TOKEN_LEN ||
      datagram_len - offset - chunk < parsed_token_len) {
    return 0;
  }

  *token = datagram + offset + chunk;
  *token_len = (size_t)parsed_token_len;
  return 1;
}

ai_quic_dispatcher_t *ai_quic_dispatcher_create(void) {
  ai_quic_dispatcher_t *dispatcher;

  dispatcher = (ai_quic_dispatcher_t *)calloc(1u, sizeof(*dispatcher));
  if (dispatcher == NULL) {
    return NULL;
  }

  dispatcher->acceptable_versions_len = ai_quic_version_supported_list(
      dispatcher->acceptable_versions,
      AI_QUIC_ARRAY_LEN(dispatcher->acceptable_versions));
  dispatcher->offered_versions_len = ai_quic_version_offered_list(
      dispatcher->offered_versions,
      AI_QUIC_ARRAY_LEN(dispatcher->offered_versions));
  dispatcher->fully_deployed_versions_len = ai_quic_version_fully_deployed_list(
      dispatcher->fully_deployed_versions,
      AI_QUIC_ARRAY_LEN(dispatcher->fully_deployed_versions));
  if (ai_quic_random_fill(dispatcher->retry_token_key,
                          sizeof(dispatcher->retry_token_key)) != AI_QUIC_OK) {
    free(dispatcher);
    return NULL;
  }
  return dispatcher;
}

void ai_quic_dispatcher_destroy(ai_quic_dispatcher_t *dispatcher) {
  free(dispatcher);
}

void ai_quic_dispatcher_set_retry_enabled(ai_quic_dispatcher_t *dispatcher, int enabled) {
  if (dispatcher == NULL) {
    return;
  }
  dispatcher->retry_enabled = enabled != 0;
}

int ai_quic_dispatcher_retry_enabled(const ai_quic_dispatcher_t *dispatcher) {
  return dispatcher != NULL && dispatcher->retry_enabled;
}

size_t ai_quic_dispatcher_supported_versions(
    const ai_quic_dispatcher_t *dispatcher,
    ai_quic_version_t *versions,
    size_t capacity) {
  size_t copy_len;

  if (dispatcher == NULL) {
    return 0u;
  }
  if (versions == NULL || capacity == 0u) {
    return dispatcher->acceptable_versions_len;
  }

  copy_len = dispatcher->acceptable_versions_len;
  if (copy_len > capacity) {
    copy_len = capacity;
  }
  memcpy(versions, dispatcher->acceptable_versions, copy_len * sizeof(*versions));
  return dispatcher->acceptable_versions_len;
}

size_t ai_quic_dispatcher_offered_versions(
    const ai_quic_dispatcher_t *dispatcher,
    ai_quic_version_t *versions,
    size_t capacity) {
  size_t copy_len;

  if (dispatcher == NULL) {
    return 0u;
  }
  if (versions == NULL || capacity == 0u) {
    return dispatcher->offered_versions_len;
  }

  copy_len = dispatcher->offered_versions_len;
  if (copy_len > capacity) {
    copy_len = capacity;
  }
  memcpy(versions, dispatcher->offered_versions, copy_len * sizeof(*versions));
  return dispatcher->offered_versions_len;
}

int ai_quic_dispatcher_is_supported_version(const ai_quic_dispatcher_t *dispatcher,
                                            ai_quic_version_t version) {
  size_t i;

  if (dispatcher == NULL) {
    return 0;
  }
  if (ai_quic_version_reserved(version)) {
    return 0;
  }
  for (i = 0; i < dispatcher->acceptable_versions_len; ++i) {
    if (dispatcher->acceptable_versions[i] == version) {
      return 1;
    }
  }
  return 0;
}

int ai_quic_dispatcher_route_datagram_from(const ai_quic_dispatcher_t *dispatcher,
                                           const uint8_t *datagram,
                                           size_t datagram_len,
                                           const uint8_t *peer_addr,
                                           size_t peer_addr_len,
                                           uint64_t now_ms,
                                           ai_quic_dispatch_decision_t *decision) {
  const uint8_t *token;
  size_t token_len;

  if (dispatcher == NULL || datagram == NULL || decision == NULL) {
    return 0;
  }

  memset(decision, 0, sizeof(*decision));
  decision->retry_enabled = dispatcher->retry_enabled;
  if (ai_quic_parse_invariant_header(datagram, datagram_len, &decision->header) !=
      AI_QUIC_OK) {
    decision->action = AI_QUIC_DISPATCH_DROP;
    return 1;
  }

  if (decision->header.type == AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION) {
    decision->action = AI_QUIC_DISPATCH_DROP;
    return 1;
  }

  if (decision->header.type == AI_QUIC_PACKET_TYPE_ONE_RTT) {
    decision->action = AI_QUIC_DISPATCH_ROUTE_EXISTING;
    return 1;
  }

  if (!ai_quic_dispatcher_is_supported_version(dispatcher, decision->header.version)) {
    decision->action = datagram_len >= AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE
                           ? AI_QUIC_DISPATCH_VERSION_NEGOTIATION
                           : AI_QUIC_DISPATCH_DROP;
    return 1;
  }

  if (decision->header.type == AI_QUIC_PACKET_TYPE_INITIAL) {
    if (dispatcher->retry_enabled &&
        ai_quic_dispatcher_parse_initial_token(&decision->header,
                                               datagram,
                                               datagram_len,
                                               &token,
                                               &token_len)) {
      decision->token_present = token_len > 0u;
      if (token_len > 0u) {
        ai_quic_retry_token_metadata_t metadata;

        if (ai_quic_retry_token_validate(dispatcher->retry_token_key,
                                         token,
                                         token_len,
                                         peer_addr,
                                         peer_addr_len,
                                         &decision->header.dcid,
                                         now_ms,
                                         &metadata) == AI_QUIC_OK) {
          decision->token_valid = 1;
          decision->original_destination_cid = metadata.original_destination_cid;
          decision->retry_source_cid = metadata.retry_source_cid;
          decision->action = AI_QUIC_DISPATCH_CREATE_CONN;
          ai_quic_log_write(AI_QUIC_LOG_INFO,
                            "dispatcher",
                            "validated retry token odcid_len=%zu retry_scid_len=%zu peer_addr_len=%zu",
                            metadata.original_destination_cid.len,
                            metadata.retry_source_cid.len,
                            peer_addr_len);
          return 1;
        }

        decision->action = AI_QUIC_DISPATCH_DROP;
        ai_quic_log_write(AI_QUIC_LOG_INFO,
                          "dispatcher",
                          "drop initial with invalid retry token token_len=%zu peer_addr_len=%zu",
                          token_len,
                          peer_addr_len);
        return 1;
      }
    }

    if (dispatcher->retry_enabled) {
      decision->original_destination_cid = decision->header.dcid;
      if (ai_quic_random_cid(&decision->retry_source_cid, 8u) != AI_QUIC_OK ||
          ai_quic_retry_token_generate(dispatcher->retry_token_key,
                                       peer_addr,
                                       peer_addr_len,
                                       &decision->original_destination_cid,
                                       &decision->retry_source_cid,
                                       now_ms,
                                       decision->retry_token,
                                       sizeof(decision->retry_token),
                                       &decision->retry_token_len) != AI_QUIC_OK) {
        decision->action = AI_QUIC_DISPATCH_DROP;
        return 1;
      }
      decision->action = AI_QUIC_DISPATCH_SEND_RETRY;
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "dispatcher",
                        "send retry token_len=%zu odcid_len=%zu retry_scid_len=%zu peer_addr_len=%zu",
                        decision->retry_token_len,
                        decision->original_destination_cid.len,
                        decision->retry_source_cid.len,
                        peer_addr_len);
      return 1;
    }

    decision->action = AI_QUIC_DISPATCH_CREATE_CONN;
  } else {
    decision->action = AI_QUIC_DISPATCH_ROUTE_EXISTING;
  }

  return 1;
}

int ai_quic_dispatcher_route_datagram(const ai_quic_dispatcher_t *dispatcher,
                                      const uint8_t *datagram,
                                      size_t datagram_len,
                                      ai_quic_dispatch_decision_t *decision) {
  return ai_quic_dispatcher_route_datagram_from(dispatcher,
                                                datagram,
                                                datagram_len,
                                                NULL,
                                                0u,
                                                0u,
                                                decision);
}
