#ifndef AI_QUIC_DISPATCHER_H
#define AI_QUIC_DISPATCHER_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/cid.h"
#include "ai_quic/packet.h"
#include "ai_quic/version.h"

typedef struct ai_quic_dispatcher ai_quic_dispatcher_t;

typedef enum ai_quic_dispatch_action {
  AI_QUIC_DISPATCH_DROP = 0,
  AI_QUIC_DISPATCH_VERSION_NEGOTIATION = 1,
  AI_QUIC_DISPATCH_ROUTE_EXISTING = 2,
  AI_QUIC_DISPATCH_CREATE_CONN = 3
} ai_quic_dispatch_action_t;

typedef struct ai_quic_dispatch_decision {
  ai_quic_dispatch_action_t action;
  ai_quic_packet_header_t header;
} ai_quic_dispatch_decision_t;

ai_quic_dispatcher_t *ai_quic_dispatcher_create(void);
void ai_quic_dispatcher_destroy(ai_quic_dispatcher_t *dispatcher);
size_t ai_quic_dispatcher_supported_versions(
    const ai_quic_dispatcher_t *dispatcher,
    ai_quic_version_t *versions,
    size_t capacity);
int ai_quic_dispatcher_is_supported_version(const ai_quic_dispatcher_t *dispatcher,
                                            ai_quic_version_t version);
int ai_quic_dispatcher_route_datagram(const ai_quic_dispatcher_t *dispatcher,
                                      const uint8_t *datagram,
                                      size_t datagram_len,
                                      ai_quic_dispatch_decision_t *decision);

#endif
