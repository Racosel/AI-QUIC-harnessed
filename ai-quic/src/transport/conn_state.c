#include "transport_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ai_quic_set_error(char *buffer, size_t capacity, const char *format, ...) {
  va_list args;

  if (buffer == NULL || capacity == 0u || format == NULL) {
    return;
  }

  va_start(args, format);
  vsnprintf(buffer, capacity, format, args);
  va_end(args);
}

ai_quic_packet_number_space_t *ai_quic_conn_space(ai_quic_conn_impl_t *conn,
                                                  ai_quic_packet_number_space_id_t id) {
  if (conn == NULL || id >= AI_QUIC_PN_SPACE_COUNT) {
    return NULL;
  }
  return &conn->packet_spaces[id];
}

const ai_quic_packet_number_space_t *ai_quic_conn_space_const(
    const ai_quic_conn_impl_t *conn,
    ai_quic_packet_number_space_id_t id) {
  if (conn == NULL || id >= AI_QUIC_PN_SPACE_COUNT) {
    return NULL;
  }
  return &conn->packet_spaces[id];
}

ai_quic_encryption_level_t ai_quic_space_to_level(
    ai_quic_packet_number_space_id_t id) {
  switch (id) {
    case AI_QUIC_PN_SPACE_INITIAL:
      return AI_QUIC_ENCRYPTION_INITIAL;
    case AI_QUIC_PN_SPACE_HANDSHAKE:
      return AI_QUIC_ENCRYPTION_HANDSHAKE;
    case AI_QUIC_PN_SPACE_APP_DATA:
    default:
      return AI_QUIC_ENCRYPTION_APPLICATION;
  }
}

ai_quic_packet_number_space_id_t ai_quic_level_to_space(
    ai_quic_encryption_level_t level) {
  switch (level) {
    case AI_QUIC_ENCRYPTION_INITIAL:
      return AI_QUIC_PN_SPACE_INITIAL;
    case AI_QUIC_ENCRYPTION_HANDSHAKE:
      return AI_QUIC_PN_SPACE_HANDSHAKE;
    case AI_QUIC_ENCRYPTION_APPLICATION:
    default:
      return AI_QUIC_PN_SPACE_APP_DATA;
  }
}
