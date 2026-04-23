#ifndef AI_QUIC_TRANSPORT_PARAMS_H
#define AI_QUIC_TRANSPORT_PARAMS_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/result.h"
#include "ai_quic/cid.h"
#include "ai_quic/version.h"

#define AI_QUIC_MAX_VERSION_INFORMATION_VERSIONS 8u

typedef struct ai_quic_version_information {
  ai_quic_version_t chosen_version;
  ai_quic_version_t available_versions[AI_QUIC_MAX_VERSION_INFORMATION_VERSIONS];
  size_t available_versions_len;
  int present;
} ai_quic_version_information_t;

typedef struct ai_quic_transport_params {
  ai_quic_cid_t initial_source_connection_id;
  ai_quic_cid_t original_destination_connection_id;
  ai_quic_cid_t retry_source_connection_id;
  ai_quic_version_information_t version_information;
  uint64_t max_udp_payload_size;
  uint64_t initial_max_data;
  uint64_t initial_max_stream_data_bidi_local;
  uint64_t initial_max_stream_data_bidi_remote;
  uint64_t initial_max_streams_bidi;
  uint64_t max_idle_timeout_ms;
  int disable_active_migration;
  int has_original_destination_connection_id;
  int has_retry_source_connection_id;
} ai_quic_transport_params_t;

void ai_quic_transport_params_init(ai_quic_transport_params_t *params);
ai_quic_result_t ai_quic_transport_params_encode(
    const ai_quic_transport_params_t *params,
    uint8_t *buffer,
    size_t capacity,
    size_t *written);
ai_quic_result_t ai_quic_transport_params_decode(
    const uint8_t *buffer,
    size_t buffer_len,
    ai_quic_transport_params_t *params);

#endif
