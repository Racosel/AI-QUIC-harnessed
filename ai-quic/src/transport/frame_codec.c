#include "transport_internal.h"

#include <string.h>

#include "common_internal.h"

ai_quic_result_t ai_quic_frame_encode(const ai_quic_frame_t *frame,
                                      uint8_t *buffer,
                                      size_t capacity,
                                      size_t *written) {
  size_t offset;
  size_t chunk;
  size_t ack_delay_len;
  size_t ack_range_count_len;
  size_t index;

  if (frame == NULL || buffer == NULL || written == NULL || capacity == 0u) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  buffer[offset++] = (uint8_t)frame->type;

  switch (frame->type) {
    case AI_QUIC_FRAME_PADDING:
    case AI_QUIC_FRAME_HANDSHAKE_DONE:
      *written = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_ACK:
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.ack.largest_acked) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               0u) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      ack_delay_len = chunk;
      offset += ack_delay_len;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.ack.ack_range_count) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      ack_range_count_len = chunk;
      offset += ack_range_count_len;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.ack.first_ack_range) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      for (index = 0u; index < frame->payload.ack.ack_range_count; ++index) {
        if (ai_quic_varint_write(buffer + offset,
                                 capacity - offset,
                                 &chunk,
                                 frame->payload.ack.ack_ranges[index].gap) != AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        offset += chunk;
        if (ai_quic_varint_write(buffer + offset,
                                 capacity - offset,
                                 &chunk,
                                 frame->payload.ack.ack_ranges[index].ack_range) !=
            AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        offset += chunk;
      }
      *written = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_CRYPTO:
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.crypto.offset) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.crypto.data_len) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (capacity - offset < frame->payload.crypto.data_len) {
        return AI_QUIC_ERROR;
      }
      memcpy(buffer + offset,
             frame->payload.crypto.data,
             frame->payload.crypto.data_len);
      offset += frame->payload.crypto.data_len;
      *written = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_MAX_DATA:
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.max_data.maximum_data) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      *written = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_MAX_STREAM_DATA: {
      size_t stream_id_len;
      size_t max_len;

      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &stream_id_len,
                               frame->payload.max_stream_data.stream_id) !=
              AI_QUIC_OK ||
          ai_quic_varint_write(buffer + offset + stream_id_len,
                               capacity - offset - stream_id_len,
                               &max_len,
                               frame->payload.max_stream_data.maximum_stream_data) !=
              AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += stream_id_len + max_len;
      *written = offset;
      return AI_QUIC_OK;
    }
    case AI_QUIC_FRAME_DATA_BLOCKED:
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.data_blocked.limit) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      *written = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_STREAM_DATA_BLOCKED: {
      size_t stream_id_len;
      size_t limit_len;

      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &stream_id_len,
                               frame->payload.stream_data_blocked.stream_id) !=
              AI_QUIC_OK ||
          ai_quic_varint_write(buffer + offset + stream_id_len,
                               capacity - offset - stream_id_len,
                               &limit_len,
                               frame->payload.stream_data_blocked.limit) !=
              AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += stream_id_len + limit_len;
      *written = offset;
      return AI_QUIC_OK;
    }
    case AI_QUIC_FRAME_STREAM:
      buffer[0] = (uint8_t)(frame->payload.stream.fin ? 0x0fu : 0x0eu);
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.stream.stream_id) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.stream.offset) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.stream.data_len) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (capacity - offset < frame->payload.stream.data_len) {
        return AI_QUIC_ERROR;
      }
      memcpy(buffer + offset,
             frame->payload.stream.data,
             frame->payload.stream.data_len);
      offset += frame->payload.stream.data_len;
      *written = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_CONNECTION_CLOSE:
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               frame->payload.connection_close.error_code) !=
              AI_QUIC_OK ||
          ai_quic_varint_write(buffer + offset + chunk,
                               capacity - offset - chunk,
                               &chunk,
                               frame->payload.connection_close.frame_type) !=
              AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_write(buffer + offset,
                               capacity - offset,
                               &chunk,
                               strlen(frame->payload.connection_close.reason)) !=
          AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (capacity - offset <
          strlen(frame->payload.connection_close.reason)) {
        return AI_QUIC_ERROR;
      }
      memcpy(buffer + offset,
             frame->payload.connection_close.reason,
             strlen(frame->payload.connection_close.reason));
      offset += strlen(frame->payload.connection_close.reason);
      *written = offset;
      return AI_QUIC_OK;
    default:
      return AI_QUIC_ERROR;
  }
}

ai_quic_result_t ai_quic_frame_decode(const uint8_t *buffer,
                                      size_t buffer_len,
                                      size_t *consumed,
                                      ai_quic_frame_t *frame) {
  size_t offset;
  size_t chunk;
  size_t ack_delay_len;
  size_t ack_range_count_len;
  size_t stored_range_count;
  size_t range_index;
  uint64_t ack_range_count;
  uint64_t value;

  if (buffer == NULL || consumed == NULL || frame == NULL || buffer_len == 0u) {
    return AI_QUIC_ERROR;
  }

  memset(frame, 0, sizeof(*frame));
  offset = 1u;
  frame->type = (ai_quic_frame_type_t)buffer[0];
  if (buffer[0] == 0x03u) {
    frame->type = AI_QUIC_FRAME_ACK;
  } else if ((buffer[0] & 0xf8u) == 0x08u) {
    frame->type = AI_QUIC_FRAME_STREAM;
  }

  switch (frame->type) {
    case AI_QUIC_FRAME_PADDING:
    case AI_QUIC_FRAME_PING:
    case AI_QUIC_FRAME_HANDSHAKE_DONE:
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_ACK:
      frame->payload.ack.ack_range_count = 0u;
      frame->payload.ack.first_ack_range = 0u;
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &frame->payload.ack.largest_acked) !=
          AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_read(buffer + offset, buffer_len - offset, &chunk, &value) !=
          AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      ack_delay_len = chunk;
      offset += ack_delay_len;
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &ack_range_count) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      ack_range_count_len = chunk;
      offset += ack_range_count_len;
      if (ai_quic_varint_read(buffer + offset, buffer_len - offset, &chunk, &value) !=
          AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      frame->payload.ack.first_ack_range = value;
      offset += chunk;
      stored_range_count = 0u;
      range_index = 0u;
      while (ack_range_count > 0u) {
        uint64_t gap;
        uint64_t ack_range;

        if (ai_quic_varint_read(buffer + offset, buffer_len - offset, &chunk, &gap) !=
                AI_QUIC_OK ||
            ai_quic_varint_read(buffer + offset + chunk,
                                buffer_len - offset - chunk,
                                &ack_delay_len,
                                &ack_range) != AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        if (stored_range_count < AI_QUIC_MAX_ACK_RANGES) {
          frame->payload.ack.ack_ranges[stored_range_count].gap = gap;
          frame->payload.ack.ack_ranges[stored_range_count].ack_range = ack_range;
          stored_range_count += 1u;
        }
        offset += chunk + ack_delay_len;
        ack_range_count -= 1u;
        range_index += 1u;
      }
      frame->payload.ack.ack_range_count = stored_range_count;
      if (buffer[0] == 0x03u) {
        for (value = 0u; value < 3u; ++value) {
          uint64_t ecn_count;

          if (ai_quic_varint_read(buffer + offset,
                                  buffer_len - offset,
                                  &chunk,
                                  &ecn_count) != AI_QUIC_OK) {
            return AI_QUIC_ERROR;
          }
          offset += chunk;
        }
      }
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_CRYPTO:
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &frame->payload.crypto.offset) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &value) != AI_QUIC_OK ||
          value > sizeof(frame->payload.crypto.data) ||
          buffer_len - offset - chunk < value) {
        return AI_QUIC_ERROR;
      }
      frame->payload.crypto.data_len = (size_t)value;
      offset += chunk;
      memcpy(frame->payload.crypto.data,
             buffer + offset,
             frame->payload.crypto.data_len);
      offset += frame->payload.crypto.data_len;
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_NEW_TOKEN:
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &value) != AI_QUIC_OK ||
          buffer_len - offset - chunk < value || value == 0u) {
        return AI_QUIC_ERROR;
      }
      offset += chunk + (size_t)value;
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_MAX_DATA:
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &frame->payload.max_data.maximum_data) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_MAX_STREAM_DATA: {
      size_t stream_id_len;
      size_t max_len;

      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &stream_id_len,
                              &frame->payload.max_stream_data.stream_id) !=
              AI_QUIC_OK ||
          ai_quic_varint_read(buffer + offset + stream_id_len,
                              buffer_len - offset - stream_id_len,
                              &max_len,
                              &frame->payload.max_stream_data.maximum_stream_data) !=
              AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += stream_id_len + max_len;
      *consumed = offset;
      return AI_QUIC_OK;
    }
    case AI_QUIC_FRAME_DATA_BLOCKED:
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &frame->payload.data_blocked.limit) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_STREAM_DATA_BLOCKED: {
      size_t stream_id_len;
      size_t limit_len;

      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &stream_id_len,
                              &frame->payload.stream_data_blocked.stream_id) !=
              AI_QUIC_OK ||
          ai_quic_varint_read(buffer + offset + stream_id_len,
                              buffer_len - offset - stream_id_len,
                              &limit_len,
                              &frame->payload.stream_data_blocked.limit) !=
              AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += stream_id_len + limit_len;
      *consumed = offset;
      return AI_QUIC_OK;
    }
    case AI_QUIC_FRAME_STREAM:
      frame->payload.stream.fin = (buffer[0] & 0x01u) != 0u;
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &frame->payload.stream.stream_id) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if ((buffer[0] & 0x04u) != 0u) {
        if (ai_quic_varint_read(buffer + offset,
                                buffer_len - offset,
                                &chunk,
                                &frame->payload.stream.offset) != AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        offset += chunk;
      } else {
        frame->payload.stream.offset = 0u;
      }
      if ((buffer[0] & 0x02u) != 0u) {
        if (ai_quic_varint_read(buffer + offset,
                                buffer_len - offset,
                                &chunk,
                                &value) != AI_QUIC_OK ||
            value > sizeof(frame->payload.stream.data) ||
            buffer_len - offset - chunk < value) {
          return AI_QUIC_ERROR;
        }
        offset += chunk;
      } else {
        value = buffer_len - offset;
        if (value > sizeof(frame->payload.stream.data)) {
          return AI_QUIC_ERROR;
        }
      }
      frame->payload.stream.data_len = (size_t)value;
      memcpy(frame->payload.stream.data,
             buffer + offset,
             frame->payload.stream.data_len);
      offset += frame->payload.stream.data_len;
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_NEW_CONNECTION_ID:
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &value) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &value) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (offset >= buffer_len) {
        return AI_QUIC_ERROR;
      }
      value = buffer[offset++];
      if (value == 0u || value > AI_QUIC_MAX_CID_LEN ||
          buffer_len - offset < value + 16u) {
        return AI_QUIC_ERROR;
      }
      offset += (size_t)value + 16u;
      *consumed = offset;
      return AI_QUIC_OK;
    case AI_QUIC_FRAME_CONNECTION_CLOSE:
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &frame->payload.connection_close.error_code) !=
              AI_QUIC_OK ||
          ai_quic_varint_read(buffer + offset + chunk,
                              buffer_len - offset - chunk,
                              &chunk,
                              &frame->payload.connection_close.frame_type) !=
              AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      if (ai_quic_varint_read(buffer + offset,
                              buffer_len - offset,
                              &chunk,
                              &value) != AI_QUIC_OK ||
          value >= sizeof(frame->payload.connection_close.reason) ||
          buffer_len - offset - chunk < value) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
      memcpy(frame->payload.connection_close.reason, buffer + offset, (size_t)value);
      frame->payload.connection_close.reason[value] = '\0';
      offset += (size_t)value;
      *consumed = offset;
      return AI_QUIC_OK;
    default:
      return AI_QUIC_ERROR;
  }
}
