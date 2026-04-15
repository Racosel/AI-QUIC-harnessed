#include "ai_quic/dispatcher.h"

#include <stdlib.h>
#include <string.h>

#include "transport_internal.h"

struct ai_quic_dispatcher {
  ai_quic_version_t supported_versions[1];
  size_t supported_versions_len;
};

ai_quic_dispatcher_t *ai_quic_dispatcher_create(void) {
  ai_quic_dispatcher_t *dispatcher;

  dispatcher = (ai_quic_dispatcher_t *)calloc(1u, sizeof(*dispatcher));
  if (dispatcher == NULL) {
    return NULL;
  }

  dispatcher->supported_versions[0] = AI_QUIC_VERSION_V1;
  dispatcher->supported_versions_len = 1u;
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
    return dispatcher->supported_versions_len;
  }

  copy_len = dispatcher->supported_versions_len;
  if (copy_len > capacity) {
    copy_len = capacity;
  }
  memcpy(versions, dispatcher->supported_versions, copy_len * sizeof(*versions));
  return dispatcher->supported_versions_len;
}

int ai_quic_dispatcher_is_supported_version(const ai_quic_dispatcher_t *dispatcher,
                                            ai_quic_version_t version) {
  size_t i;

  if (dispatcher == NULL) {
    return 0;
  }
  for (i = 0; i < dispatcher->supported_versions_len; ++i) {
    if (dispatcher->supported_versions[i] == version) {
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
