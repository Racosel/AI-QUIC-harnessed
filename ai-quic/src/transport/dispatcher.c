#include "ai_quic/dispatcher.h"

#include <stdlib.h>
#include <string.h>

#include "transport_internal.h"

struct ai_quic_dispatcher {
  ai_quic_version_t acceptable_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t acceptable_versions_len;
  ai_quic_version_t offered_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t offered_versions_len;
  ai_quic_version_t fully_deployed_versions[AI_QUIC_MAX_SUPPORTED_VERSIONS];
  size_t fully_deployed_versions_len;
};

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
  return dispatcher;
}

void ai_quic_dispatcher_destroy(ai_quic_dispatcher_t *dispatcher) {
  free(dispatcher);
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

int ai_quic_dispatcher_route_datagram(const ai_quic_dispatcher_t *dispatcher,
                                      const uint8_t *datagram,
                                      size_t datagram_len,
                                      ai_quic_dispatch_decision_t *decision) {
  if (dispatcher == NULL || datagram == NULL || decision == NULL) {
    return 0;
  }

  memset(decision, 0, sizeof(*decision));
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
    decision->action = AI_QUIC_DISPATCH_CREATE_CONN;
  } else {
    decision->action = AI_QUIC_DISPATCH_ROUTE_EXISTING;
  }

  return 1;
}
