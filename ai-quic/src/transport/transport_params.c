#include "transport_internal.h"

#include <string.h>

#include "common_internal.h"

#define AI_QUIC_TP_ORIGINAL_DESTINATION_CONNECTION_ID 0x00u
#define AI_QUIC_TP_MAX_IDLE_TIMEOUT 0x01u
#define AI_QUIC_TP_MAX_UDP_PAYLOAD_SIZE 0x03u
#define AI_QUIC_TP_INITIAL_MAX_DATA 0x04u
#define AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL 0x05u
#define AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE 0x06u
#define AI_QUIC_TP_INITIAL_MAX_STREAMS_BIDI 0x08u
#define AI_QUIC_TP_DISABLE_ACTIVE_MIGRATION 0x0cu
#define AI_QUIC_TP_INITIAL_SOURCE_CONNECTION_ID 0x0fu
#define AI_QUIC_TP_RETRY_SOURCE_CONNECTION_ID 0x10u
#define AI_QUIC_TP_VERSION_INFORMATION 0x11u

static ai_quic_result_t ai_quic_transport_params_write_varint_param(
    uint8_t *buffer,
    size_t capacity,
    size_t *offset,
    uint64_t id,
    uint64_t value) {
  size_t id_len;
  size_t value_len_len;
  size_t encoded_value_len;

  encoded_value_len = ai_quic_varint_size(value);
  if (ai_quic_varint_write(buffer + *offset, capacity - *offset, &id_len, id) !=
          AI_QUIC_OK ||
      capacity - *offset - id_len <
          ai_quic_varint_size(encoded_value_len) + encoded_value_len) {
    return AI_QUIC_ERROR;
  }

  *offset += id_len;
  if (ai_quic_varint_write(buffer + *offset,
                           capacity - *offset,
                           &value_len_len,
                           encoded_value_len) != AI_QUIC_OK ||
      ai_quic_varint_write(buffer + *offset + value_len_len,
                           capacity - *offset - value_len_len,
                           &encoded_value_len,
                           value) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  *offset += value_len_len + encoded_value_len;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_transport_params_write_bytes_param(
    uint8_t *buffer,
    size_t capacity,
    size_t *offset,
    uint64_t id,
    const uint8_t *value,
    size_t value_len) {
  size_t chunk;
  size_t value_len_field;

  if (value == NULL) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_varint_write(buffer + *offset, capacity - *offset, &chunk, id) !=
          AI_QUIC_OK ||
      capacity - *offset - chunk < ai_quic_varint_size(value_len) + value_len) {
    return AI_QUIC_ERROR;
  }
  *offset += chunk;

  if (ai_quic_varint_write(buffer + *offset,
                           capacity - *offset,
                           &value_len_field,
                           value_len) != AI_QUIC_OK ||
      capacity - *offset - value_len_field < value_len) {
    return AI_QUIC_ERROR;
  }
  *offset += value_len_field;

  memcpy(buffer + *offset, value, value_len);
  *offset += value_len;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_transport_params_write_flag_param(
    uint8_t *buffer,
    size_t capacity,
    size_t *offset,
    uint64_t id) {
  size_t chunk;

  if (ai_quic_varint_write(buffer + *offset, capacity - *offset, &chunk, id) !=
          AI_QUIC_OK ||
      capacity - *offset - chunk < 1u) {
    return AI_QUIC_ERROR;
  }
  *offset += chunk;
  buffer[(*offset)++] = 0u;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_transport_params_parse_cid(
    ai_quic_cid_t *cid,
    const uint8_t *value,
    size_t value_len) {
  if (cid == NULL || value == NULL || value_len > AI_QUIC_MAX_CID_LEN) {
    return AI_QUIC_ERROR;
  }
  return ai_quic_cid_from_bytes(cid, value, value_len) ? AI_QUIC_OK : AI_QUIC_ERROR;
}

static ai_quic_result_t ai_quic_transport_params_write_version_information(
    uint8_t *buffer,
    size_t capacity,
    size_t *offset,
    const ai_quic_version_information_t *version_information) {
  size_t chunk;
  size_t value_len_field;
  size_t i;
  size_t value_len;

  if (buffer == NULL || offset == NULL || version_information == NULL ||
      !version_information->present || version_information->chosen_version == 0u ||
      version_information->available_versions_len > AI_QUIC_MAX_VERSION_INFORMATION_VERSIONS) {
    return AI_QUIC_ERROR;
  }

  value_len = (1u + version_information->available_versions_len) * sizeof(uint32_t);
  if (ai_quic_varint_write(
          buffer + *offset, capacity - *offset, &chunk, AI_QUIC_TP_VERSION_INFORMATION) !=
          AI_QUIC_OK ||
      capacity - *offset - chunk < ai_quic_varint_size(value_len) + value_len) {
    return AI_QUIC_ERROR;
  }
  *offset += chunk;

  if (ai_quic_varint_write(buffer + *offset,
                           capacity - *offset,
                           &value_len_field,
                           value_len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  *offset += value_len_field;

  if (ai_quic_write_u32(buffer + *offset,
                        capacity - *offset,
                        &chunk,
                        version_information->chosen_version) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  *offset += chunk;

  for (i = 0u; i < version_information->available_versions_len; ++i) {
    if (version_information->available_versions[i] == 0u ||
        ai_quic_write_u32(buffer + *offset,
                          capacity - *offset,
                          &chunk,
                          version_information->available_versions[i]) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    *offset += chunk;
  }
  return AI_QUIC_OK;
}

void ai_quic_transport_params_init(ai_quic_transport_params_t *params) {
  if (params == NULL) {
    return;
  }

  memset(params, 0, sizeof(*params));
  params->max_udp_payload_size = 65527u;
  params->initial_max_data = AI_QUIC_INITIAL_MAX_DATA;
  params->initial_max_stream_data_bidi_local = AI_QUIC_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL;
  params->initial_max_stream_data_bidi_remote = AI_QUIC_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE;
  params->initial_max_streams_bidi = 32u;
  params->max_idle_timeout_ms = 30000u;
}

ai_quic_result_t ai_quic_transport_params_encode(
    const ai_quic_transport_params_t *params,
    uint8_t *buffer,
    size_t capacity,
    size_t *written) {
  size_t offset;

  if (params == NULL || buffer == NULL || written == NULL) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  if (params->has_original_destination_connection_id &&
      ai_quic_transport_params_write_bytes_param(
          buffer,
          capacity,
          &offset,
          AI_QUIC_TP_ORIGINAL_DESTINATION_CONNECTION_ID,
          params->original_destination_connection_id.bytes,
          params->original_destination_connection_id.len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  if (ai_quic_transport_params_write_varint_param(buffer,
                                                  capacity,
                                                  &offset,
                                                  AI_QUIC_TP_MAX_IDLE_TIMEOUT,
                                                  params->max_idle_timeout_ms) !=
          AI_QUIC_OK ||
      ai_quic_transport_params_write_varint_param(buffer,
                                                  capacity,
                                                  &offset,
                                                  AI_QUIC_TP_MAX_UDP_PAYLOAD_SIZE,
                                                  params->max_udp_payload_size) !=
          AI_QUIC_OK ||
      ai_quic_transport_params_write_varint_param(buffer,
                                                  capacity,
                                                  &offset,
                                                  AI_QUIC_TP_INITIAL_MAX_DATA,
                                                  params->initial_max_data) !=
          AI_QUIC_OK ||
      ai_quic_transport_params_write_varint_param(
          buffer,
          capacity,
          &offset,
          AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL,
          params->initial_max_stream_data_bidi_local) != AI_QUIC_OK ||
      ai_quic_transport_params_write_varint_param(
          buffer,
          capacity,
          &offset,
          AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE,
          params->initial_max_stream_data_bidi_remote) != AI_QUIC_OK ||
      ai_quic_transport_params_write_varint_param(
          buffer,
          capacity,
          &offset,
          AI_QUIC_TP_INITIAL_MAX_STREAMS_BIDI,
          params->initial_max_streams_bidi) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  if (params->disable_active_migration &&
      ai_quic_transport_params_write_flag_param(buffer,
                                                capacity,
                                                &offset,
                                                AI_QUIC_TP_DISABLE_ACTIVE_MIGRATION) !=
          AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  if (ai_quic_transport_params_write_bytes_param(
          buffer,
          capacity,
          &offset,
          AI_QUIC_TP_INITIAL_SOURCE_CONNECTION_ID,
          params->initial_source_connection_id.bytes,
          params->initial_source_connection_id.len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  if (params->has_retry_source_connection_id &&
      ai_quic_transport_params_write_bytes_param(
          buffer,
          capacity,
          &offset,
          AI_QUIC_TP_RETRY_SOURCE_CONNECTION_ID,
          params->retry_source_connection_id.bytes,
          params->retry_source_connection_id.len) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  if (params->version_information.present &&
      ai_quic_transport_params_write_version_information(
          buffer, capacity, &offset, &params->version_information) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  *written = offset;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_transport_params_decode(
    const uint8_t *buffer,
    size_t buffer_len,
    ai_quic_transport_params_t *params) {
  size_t offset;
  size_t consumed;
  uint64_t id;
  uint64_t value_len;
  uint64_t value;

  if (buffer == NULL || params == NULL) {
    return AI_QUIC_ERROR;
  }

  ai_quic_transport_params_init(params);
  offset = 0u;

  while (offset < buffer_len) {
    if (ai_quic_varint_read(buffer + offset, buffer_len - offset, &consumed, &id) !=
            AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    offset += consumed;

    if (ai_quic_varint_read(buffer + offset,
                            buffer_len - offset,
                            &consumed,
                            &value_len) != AI_QUIC_OK ||
        buffer_len - offset - consumed < value_len) {
      return AI_QUIC_ERROR;
    }
    offset += consumed;

    switch (id) {
      case AI_QUIC_TP_ORIGINAL_DESTINATION_CONNECTION_ID:
        if (ai_quic_transport_params_parse_cid(
                &params->original_destination_connection_id,
                buffer + offset,
                (size_t)value_len) != AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        params->has_original_destination_connection_id = 1;
        break;
      case AI_QUIC_TP_MAX_IDLE_TIMEOUT:
      case AI_QUIC_TP_MAX_UDP_PAYLOAD_SIZE:
      case AI_QUIC_TP_INITIAL_MAX_DATA:
      case AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL:
      case AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE:
      case AI_QUIC_TP_INITIAL_MAX_STREAMS_BIDI:
        if (ai_quic_varint_read(buffer + offset,
                                (size_t)value_len,
                                &consumed,
                                &value) != AI_QUIC_OK ||
            consumed != value_len) {
          return AI_QUIC_ERROR;
        }
        if (id == AI_QUIC_TP_MAX_IDLE_TIMEOUT) {
          params->max_idle_timeout_ms = value;
        } else if (id == AI_QUIC_TP_MAX_UDP_PAYLOAD_SIZE) {
          params->max_udp_payload_size = value;
        } else if (id == AI_QUIC_TP_INITIAL_MAX_DATA) {
          params->initial_max_data = value;
        } else if (id == AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL) {
          params->initial_max_stream_data_bidi_local = value;
        } else if (id == AI_QUIC_TP_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE) {
          params->initial_max_stream_data_bidi_remote = value;
        } else {
          params->initial_max_streams_bidi = value;
        }
        break;
      case AI_QUIC_TP_DISABLE_ACTIVE_MIGRATION:
        if (value_len != 0u) {
          return AI_QUIC_ERROR;
        }
        params->disable_active_migration = 1;
        break;
      case AI_QUIC_TP_INITIAL_SOURCE_CONNECTION_ID:
        if (ai_quic_transport_params_parse_cid(
                &params->initial_source_connection_id,
                buffer + offset,
                (size_t)value_len) != AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        break;
      case AI_QUIC_TP_RETRY_SOURCE_CONNECTION_ID:
        if (ai_quic_transport_params_parse_cid(
                &params->retry_source_connection_id,
                buffer + offset,
                (size_t)value_len) != AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        params->has_retry_source_connection_id = 1;
        break;
      case AI_QUIC_TP_VERSION_INFORMATION:
        if (value_len < sizeof(uint32_t) || (value_len % sizeof(uint32_t)) != 0u ||
            ((value_len / sizeof(uint32_t)) - 1u) >
                AI_QUIC_MAX_VERSION_INFORMATION_VERSIONS) {
          return AI_QUIC_ERROR;
        }
        if (ai_quic_read_u32(buffer + offset,
                             (size_t)value_len,
                             &consumed,
                             &params->version_information.chosen_version) != AI_QUIC_OK ||
            params->version_information.chosen_version == 0u) {
          return AI_QUIC_ERROR;
        }
        params->version_information.present = 1;
        params->version_information.available_versions_len =
            (size_t)(value_len / sizeof(uint32_t)) - 1u;
        {
          size_t i;
          size_t read_offset;
          uint32_t version;

          read_offset = offset + consumed;
          for (i = 0u; i < params->version_information.available_versions_len; ++i) {
            if (ai_quic_read_u32(buffer + read_offset,
                                 (size_t)value_len - (read_offset - offset),
                                 &consumed,
                                 &version) != AI_QUIC_OK ||
                version == 0u) {
              return AI_QUIC_ERROR;
            }
            params->version_information.available_versions[i] = version;
            read_offset += consumed;
          }
        }
        break;
      default:
        break;
    }

    offset += (size_t)value_len;
  }

  if (params->initial_source_connection_id.len == 0u) {
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_transport_params_validate_client(
    const ai_quic_transport_params_t *params,
    const ai_quic_cid_t *original_destination_cid,
    const ai_quic_cid_t *peer_scid,
    const ai_quic_cid_t *retry_source_cid,
    int retry_accepted) {
  if (params == NULL || original_destination_cid == NULL || peer_scid == NULL) {
    return AI_QUIC_ERROR;
  }
  if (!params->has_original_destination_connection_id ||
      !ai_quic_cid_equal(&params->original_destination_connection_id,
                         original_destination_cid)) {
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_cid_equal(&params->initial_source_connection_id, peer_scid)) {
    return AI_QUIC_ERROR;
  }
  if (retry_accepted) {
    if (retry_source_cid == NULL || !params->has_retry_source_connection_id ||
        !ai_quic_cid_equal(&params->retry_source_connection_id, retry_source_cid)) {
      return AI_QUIC_ERROR;
    }
  } else if (params->has_retry_source_connection_id) {
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_transport_params_validate_server(
    const ai_quic_transport_params_t *params,
    const ai_quic_cid_t *peer_scid) {
  if (params == NULL || peer_scid == NULL) {
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_cid_equal(&params->initial_source_connection_id, peer_scid)) {
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_transport_params_validate_client_version_information(
    const ai_quic_version_information_t *version_information,
    ai_quic_version_t packet_version,
    int require_present,
    uint64_t *transport_error_code) {
  if (transport_error_code != NULL) {
    *transport_error_code = AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION;
  }
  if (version_information == NULL || !version_information->present) {
    return require_present ? AI_QUIC_ERROR : AI_QUIC_OK;
  }
  if (!ai_quic_version_information_contains(version_information,
                                            version_information->chosen_version)) {
    if (transport_error_code != NULL) {
      *transport_error_code = AI_QUIC_TRANSPORT_ERROR_TRANSPORT_PARAMETER;
    }
    return AI_QUIC_ERROR;
  }
  if (version_information->chosen_version != packet_version) {
    if (transport_error_code != NULL) {
      *transport_error_code = AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION;
    }
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_transport_params_validate_server_version_information(
    const ai_quic_version_information_t *server_version_information,
    const ai_quic_version_information_t *client_version_information,
    ai_quic_version_t negotiated_version,
    int require_present,
    uint64_t *transport_error_code) {
  if (transport_error_code != NULL) {
    *transport_error_code = AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION;
  }
  if (server_version_information == NULL || !server_version_information->present) {
    return require_present ? AI_QUIC_ERROR : AI_QUIC_OK;
  }
  if (client_version_information == NULL ||
      !ai_quic_version_information_contains(client_version_information,
                                            server_version_information->chosen_version) ||
      server_version_information->chosen_version != negotiated_version) {
    if (transport_error_code != NULL) {
      *transport_error_code = AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION;
    }
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}
