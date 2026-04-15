#include "transport_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_quic/log.h"
#include "ai_quic/fs.h"

#define AI_QUIC_APP_DATA_PACKET_BUDGET 8u
#define AI_QUIC_APP_DATA_RETRANSMIT_BUDGET 4u
#define AI_QUIC_APP_DATA_RETRANSMIT_ONLY_PACKET_BUDGET 16u
#define AI_QUIC_APP_DATA_RETRANSMIT_ONLY_BUDGET \
  AI_QUIC_APP_DATA_RETRANSMIT_ONLY_PACKET_BUDGET
#define AI_QUIC_APP_DATA_NEW_DATA_BUDGET 5u
#define AI_QUIC_APP_DATA_THROTTLED_NEW_DATA_BUDGET 2u
#define AI_QUIC_PEER_BLOCKED_COOLDOWN_MS 250u
#define AI_QUIC_PEER_CREDIT_UPDATE_COOLDOWN_MS 250u
#define AI_QUIC_PTO_RETRANSMIT_TAIL_CHUNKS 16u

typedef struct ai_quic_app_flush_stats {
  size_t packets;
  size_t control_frames;
  size_t retransmit_frames;
  size_t new_data_frames;
  size_t conn_blocked_streams;
  size_t stream_blocked_streams;
} ai_quic_app_flush_stats_t;

typedef enum ai_quic_stream_send_mode {
  AI_QUIC_STREAM_SEND_MODE_RETRANSMIT = 0,
  AI_QUIC_STREAM_SEND_MODE_NEW_DATA = 1
} ai_quic_stream_send_mode_t;

static ai_quic_packet_number_space_id_t ai_quic_packet_type_to_space(
    ai_quic_packet_type_t type);
static int ai_quic_packet_is_ack_eliciting(const ai_quic_packet_t *packet);
static uint64_t ai_quic_stream_tail_resend_start(uint64_t resend_limit);
static int ai_quic_conn_has_pending_new_stream_data(const ai_quic_conn_impl_t *conn);

static const char *ai_quic_qlog_packet_type_name(ai_quic_packet_type_t type) {
  switch (type) {
    case AI_QUIC_PACKET_TYPE_INITIAL:
      return "initial";
    case AI_QUIC_PACKET_TYPE_HANDSHAKE:
      return "handshake";
    case AI_QUIC_PACKET_TYPE_ONE_RTT:
      return "1rtt";
    case AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION:
      return "version_negotiation";
    default:
      return "unknown";
  }
}

static const char *ai_quic_space_name(ai_quic_packet_number_space_id_t id) {
  switch (id) {
    case AI_QUIC_PN_SPACE_INITIAL:
      return "initial";
    case AI_QUIC_PN_SPACE_HANDSHAKE:
      return "handshake";
    case AI_QUIC_PN_SPACE_APP_DATA:
      return "app";
    default:
      return "unknown";
  }
}

static const char *ai_quic_tls_event_name(ai_quic_tls_event_type_t type) {
  switch (type) {
    case AI_QUIC_TLS_EVENT_WRITE_CRYPTO:
      return "write_crypto";
    case AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS:
      return "install_handshake_keys";
    case AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS:
      return "install_1rtt_keys";
    case AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE:
      return "handshake_complete";
    case AI_QUIC_TLS_EVENT_NONE:
    default:
      return "none";
  }
}

static void ai_quic_format_cid_hex(const ai_quic_cid_t *cid,
                                   char *buffer,
                                   size_t capacity) {
  static const char kHex[] = "0123456789abcdef";
  size_t i;
  size_t offset;

  if (buffer == NULL || capacity == 0u) {
    return;
  }
  if (cid == NULL) {
    buffer[0] = '\0';
    return;
  }

  offset = 0u;
  for (i = 0; i < cid->len && offset + 2u < capacity; ++i) {
    buffer[offset++] = kHex[(cid->bytes[i] >> 4u) & 0x0fu];
    buffer[offset++] = kHex[cid->bytes[i] & 0x0fu];
  }
  buffer[offset] = '\0';
}

static void ai_quic_format_preview_hex(const uint8_t *bytes,
                                       size_t len,
                                       char *buffer,
                                       size_t capacity) {
  static const char kHex[] = "0123456789abcdef";
  size_t preview_len;
  size_t i;
  size_t offset;

  if (buffer == NULL || capacity == 0u) {
    return;
  }
  buffer[0] = '\0';
  if (bytes == NULL || len == 0u) {
    return;
  }

  preview_len = len < 24u ? len : 24u;
  offset = 0u;
  for (i = 0; i < preview_len && offset + 2u < capacity; ++i) {
    buffer[offset++] = kHex[(bytes[i] >> 4u) & 0x0fu];
    buffer[offset++] = kHex[bytes[i] & 0x0fu];
  }
  if (preview_len < len && offset + 4u < capacity) {
    buffer[offset++] = '.';
    buffer[offset++] = '.';
    buffer[offset++] = '.';
  }
  buffer[offset] = '\0';
}

static void ai_quic_qlog_packet(ai_quic_conn_impl_t *conn,
                                uint64_t now_ms,
                                const char *event_name,
                                const ai_quic_packet_t *packet,
                                size_t packet_length,
                                const uint8_t *raw_bytes,
                                size_t raw_len) {
  char dcid[AI_QUIC_MAX_CID_LEN * 2u + 1u];
  char scid[AI_QUIC_MAX_CID_LEN * 2u + 1u];
  char preview[64];
  char data_json[512];

  if (conn == NULL || conn->qlog == NULL || event_name == NULL || packet == NULL) {
    return;
  }

  ai_quic_format_cid_hex(&packet->header.dcid, dcid, sizeof(dcid));
  ai_quic_format_cid_hex(&packet->header.scid, scid, sizeof(scid));
  ai_quic_format_preview_hex(raw_bytes, raw_len, preview, sizeof(preview));
  snprintf(data_json,
           sizeof(data_json),
           "{\"header\":{\"packet_type\":\"%s\",\"packet_number\":%llu,"
           "\"dcid\":\"%s\",\"scid\":\"%s\",\"version\":%u},\"raw\":{\"length\":%zu},"
           "\"preview\":\"%s\",\"frames\":[{\"frame_count\":%zu}]}",
           ai_quic_qlog_packet_type_name(packet->header.type),
           (unsigned long long)packet->header.packet_number,
           dcid,
           scid,
           packet->header.version,
           packet_length,
           preview,
           packet->frame_count);
  ai_quic_qlog_write_event(conn->qlog, now_ms, "transport", event_name, data_json);
}

static ai_quic_result_t ai_quic_conn_push_datagram(
    ai_quic_conn_impl_t *conn,
    ai_quic_packet_t *packets,
    size_t packet_count,
    ai_quic_pending_datagram_t *pending,
    size_t *pending_count,
    size_t pending_capacity) {
  size_t offset;
  size_t i;
  size_t written;

  if (conn == NULL || packets == NULL || pending == NULL || pending_count == NULL ||
      *pending_count >= pending_capacity) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  for (i = 0; i < packet_count; ++i) {
    size_t packet_written;

    if (ai_quic_packet_encode_conn(conn,
                                   &packets[i],
                                   pending[*pending_count].bytes + offset,
                                   sizeof(pending[*pending_count].bytes) - offset,
                                   &written) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error,
                        sizeof(conn->last_error),
                        "packet encode failed type=%d pn=%llu detail=%s",
                        (int)packets[i].header.type,
                        (unsigned long long)packets[i].header.packet_number,
                        ai_quic_packet_encode_last_error());
      return AI_QUIC_ERROR;
    }
    packet_written = written;
    packets[i].header.packet_length = packet_written;
    ai_quic_qlog_packet(conn,
                        ai_quic_now_ms(),
                        "packet_sent",
                        &packets[i],
                        packet_written,
                        pending[*pending_count].bytes + offset,
                        packet_written);
    offset += written;
  }

  if (conn->is_server && !conn->address_validated &&
      conn->bytes_sent + offset > conn->bytes_received * 3u) {
    ai_quic_set_error(conn->last_error,
                      sizeof(conn->last_error),
                      "anti-amplification budget exceeded");
    return AI_QUIC_ERROR;
  }

  if (!conn->is_server && packet_count == 1u &&
      packets[0].header.type == AI_QUIC_PACKET_TYPE_INITIAL &&
      offset < AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE) {
    memset(pending[*pending_count].bytes + offset,
           0,
           AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE - offset);
    offset = AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE;
  }

  pending[*pending_count].len = offset;
  *pending_count += 1u;
  conn->bytes_sent += offset;
  for (i = 0; i < packet_count; ++i) {
    if (!ai_quic_packet_is_ack_eliciting(&packets[i])) {
      continue;
    }
    ai_quic_loss_on_packet_sent(conn,
                                ai_quic_packet_type_to_space(packets[i].header.type),
                                packets[i].header.packet_number,
                                ai_quic_now_ms());
  }
  return AI_QUIC_OK;
}

static ai_quic_packet_number_space_id_t ai_quic_packet_type_to_space(
    ai_quic_packet_type_t type) {
  switch (type) {
    case AI_QUIC_PACKET_TYPE_INITIAL:
      return AI_QUIC_PN_SPACE_INITIAL;
    case AI_QUIC_PACKET_TYPE_HANDSHAKE:
      return AI_QUIC_PN_SPACE_HANDSHAKE;
    case AI_QUIC_PACKET_TYPE_ONE_RTT:
    default:
      return AI_QUIC_PN_SPACE_APP_DATA;
  }
}

static int ai_quic_packet_is_ack_eliciting(const ai_quic_packet_t *packet) {
  size_t i;

  if (packet == NULL) {
    return 0;
  }

  for (i = 0; i < packet->frame_count; ++i) {
    ai_quic_frame_type_t type = packet->frames[i].type;
    if (type != AI_QUIC_FRAME_ACK && type != AI_QUIC_FRAME_PADDING) {
      return 1;
    }
  }
  return 0;
}

static uint64_t ai_quic_stream_tail_resend_start(uint64_t resend_limit) {
  uint64_t tail_bytes;

  tail_bytes = AI_QUIC_PTO_RETRANSMIT_TAIL_CHUNKS * AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN;
  if (resend_limit <= tail_bytes) {
    return 0u;
  }
  return resend_limit - tail_bytes;
}

static void ai_quic_packet_init(ai_quic_packet_t *packet,
                                ai_quic_packet_type_t type,
                                ai_quic_conn_impl_t *conn,
                                ai_quic_packet_number_space_id_t space_id) {
  memset(packet, 0, sizeof(*packet));
  packet->header.type = type;
  packet->header.version = conn->version;
  packet->header.dcid = conn->peer_cid;
  packet->header.scid = conn->local_cid;
  packet->header.packet_number =
      conn->packet_spaces[space_id].next_packet_number++;
}

static ai_quic_result_t ai_quic_conn_build_crypto_packet(
    ai_quic_conn_impl_t *conn,
    ai_quic_encryption_level_t level,
    const uint8_t *data,
    size_t data_len,
    ai_quic_packet_t *packet) {
  ai_quic_packet_number_space_id_t space_id;
  ai_quic_packet_type_t type;
  ai_quic_packet_number_space_t *space;
  size_t frame_index;

  space_id = ai_quic_level_to_space(level);
  type = space_id == AI_QUIC_PN_SPACE_INITIAL ? AI_QUIC_PACKET_TYPE_INITIAL
                                              : AI_QUIC_PACKET_TYPE_HANDSHAKE;
  ai_quic_packet_init(packet, type, conn, space_id);
  space = ai_quic_conn_space(conn, space_id);
  frame_index = 0u;

  if (space->ack_needed && space->largest_received_packet_number != UINT64_MAX) {
    ai_quic_build_ack_frame(space, &packet->frames[frame_index++]);
    space->ack_needed = 0;
  }

  packet->frames[frame_index].type = AI_QUIC_FRAME_CRYPTO;
  packet->frames[frame_index].payload.crypto.offset = space->crypto_stream.tx_next_offset;
  packet->frames[frame_index].payload.crypto.data_len = data_len;
  memcpy(packet->frames[frame_index].payload.crypto.data, data, data_len);
  space->crypto_stream.tx_next_offset += data_len;
  frame_index += 1u;
  packet->frame_count = frame_index;

  if (type == AI_QUIC_PACKET_TYPE_INITIAL) {
    packet->header.token_len = 0u;
  }
  if (!conn->is_server && type == AI_QUIC_PACKET_TYPE_HANDSHAKE &&
      ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_INITIAL)->key_state ==
          AI_QUIC_KEY_STATE_INSTALLED) {
    ai_quic_loss_discard_space(conn, AI_QUIC_PN_SPACE_INITIAL);
    ai_quic_qlog_write_key_value(conn->qlog,
                                 ai_quic_now_ms(),
                                 "security",
                                 "key_discarded",
                                 "key_type",
                                 "initial");
  }
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_build_ack_packet(
    ai_quic_conn_impl_t *conn,
    ai_quic_packet_number_space_id_t space_id,
    ai_quic_packet_t *packet) {
  ai_quic_packet_number_space_t *space;
  ai_quic_packet_type_t type;

  if (conn == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL || !space->ack_needed ||
      space->largest_received_packet_number == UINT64_MAX ||
      space->key_state != AI_QUIC_KEY_STATE_INSTALLED) {
    return AI_QUIC_ERROR;
  }

  if (space_id == AI_QUIC_PN_SPACE_INITIAL) {
    type = AI_QUIC_PACKET_TYPE_INITIAL;
  } else if (space_id == AI_QUIC_PN_SPACE_HANDSHAKE) {
    type = AI_QUIC_PACKET_TYPE_HANDSHAKE;
  } else {
    type = AI_QUIC_PACKET_TYPE_ONE_RTT;
  }
  ai_quic_packet_init(packet, type, conn, space_id);
  ai_quic_build_ack_frame(space, &packet->frames[0]);
  packet->frame_count = 1u;
  if (type == AI_QUIC_PACKET_TYPE_INITIAL) {
    packet->header.token_len = 0u;
  }
  space->ack_needed = 0;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_send_handshake_done(
    ai_quic_conn_impl_t *conn,
    ai_quic_pending_datagram_t *pending,
    size_t *pending_count,
    size_t pending_capacity) {
  ai_quic_packet_t packet;
  ai_quic_packet_init(&packet, AI_QUIC_PACKET_TYPE_ONE_RTT, conn, AI_QUIC_PN_SPACE_APP_DATA);
  packet.frames[0].type = AI_QUIC_FRAME_HANDSHAKE_DONE;
  packet.frame_count = 1u;
  conn->handshake_confirmed = 1;
  return ai_quic_conn_push_datagram(conn, &packet, 1u, pending, pending_count, pending_capacity);
}

static uint64_t ai_quic_conn_stream_send_limit(const ai_quic_conn_impl_t *conn,
                                               int is_local_stream) {
  if (conn == NULL) {
    return 0u;
  }
  return is_local_stream ? conn->peer_transport_params.initial_max_stream_data_bidi_remote
                         : conn->peer_transport_params.initial_max_stream_data_bidi_local;
}

static uint64_t ai_quic_conn_stream_recv_limit(const ai_quic_conn_impl_t *conn,
                                               int is_local_stream) {
  if (conn == NULL) {
    return 0u;
  }
  return is_local_stream ? conn->local_transport_params.initial_max_stream_data_bidi_local
                         : conn->local_transport_params.initial_max_stream_data_bidi_remote;
}

static void ai_quic_conn_apply_peer_transport_params(ai_quic_conn_impl_t *conn) {
  size_t i;

  if (conn == NULL) {
    return;
  }

  ai_quic_flow_controller_set_send_limit(&conn->conn_flow,
                                         conn->peer_transport_params.initial_max_data);
  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++i) {
    ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    if (!stream->in_use) {
      continue;
    }
    ai_quic_flow_controller_set_send_limit(
        &stream->flow,
        ai_quic_conn_stream_send_limit(conn, stream->is_local));
  }
}

static uint64_t ai_quic_conn_total_sent_bytes(const ai_quic_conn_impl_t *conn) {
  uint64_t total;
  size_t i;

  total = 0u;
  if (conn == NULL) {
    return 0u;
  }
  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++i) {
    const ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    if (stream->in_use) {
      total += stream->send_offset;
    }
  }
  return total;
}

static ai_quic_result_t ai_quic_conn_append_file(const char *path,
                                                 const uint8_t *data,
                                                 size_t data_len) {
  FILE *file;

  if (path == NULL || path[0] == '\0') {
    return AI_QUIC_ERROR;
  }

  file = fopen(path, "ab");
  if (file == NULL) {
    return AI_QUIC_ERROR;
  }
  if (data_len > 0u && fwrite(data, 1u, data_len, file) != data_len) {
    fclose(file);
    return AI_QUIC_ERROR;
  }
  if (fclose(file) != 0) {
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_on_app_consumed(ai_quic_conn_impl_t *conn,
                                                     ai_quic_stream_state_t *stream,
                                                     uint64_t bytes) {
  uint64_t update_margin;

  if (conn == NULL || stream == NULL) {
    return AI_QUIC_ERROR;
  }
  if (ai_quic_stream_consume(stream, bytes) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  conn->conn_flow.recv_consumed += bytes;
  update_margin = conn->conn_flow.initial_window / 2u;
  update_margin += AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN;
  if (conn->conn_flow.initial_window > 0u &&
      conn->conn_flow.recv_consumed + update_margin >= conn->conn_flow.recv_limit) {
    conn->conn_flow.recv_limit = conn->conn_flow.recv_consumed + conn->conn_flow.initial_window;
    conn->conn_flow.update_pending = 1;
  }
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_prepare_response_stream(ai_quic_conn_impl_t *conn,
                                                             ai_quic_stream_state_t *stream,
                                                             const char *www_root) {
  char filename[256];
  char full_path[1024];
  uint8_t *file_data;
  size_t file_len;
  ai_quic_result_t rc;

  if (conn == NULL || stream == NULL || www_root == NULL) {
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_path_extract_filename(stream->request_path, filename, sizeof(filename))) {
    return AI_QUIC_ERROR;
  }

  snprintf(full_path, sizeof(full_path), "%s/%s", www_root, filename);
  if (ai_quic_fs_read_binary_file(full_path, &file_data, &file_len) != AI_QUIC_OK) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "unable to read %s", full_path);
    return AI_QUIC_ERROR;
  }

  rc = ai_quic_stream_prepare_send(stream,
                                   file_data,
                                   file_len,
                                   1,
                                   stream->request_path,
                                   NULL);
  free(file_data);
  if (rc != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  stream->response_prepared = 1;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_process_server_stream(ai_quic_conn_impl_t *conn,
                                                           ai_quic_stream_state_t *stream,
                                                           const char *www_root) {
  size_t copy_len;
  char request_line[AI_QUIC_MAX_STREAM_REQUEST_PATH_LEN + 32u];

  if (conn == NULL || stream == NULL || !stream->recv_fin || stream->request_parsed) {
    return AI_QUIC_OK;
  }
  if (!stream->final_size_known || stream->recv_contiguous_end != stream->final_size) {
    return AI_QUIC_OK;
  }
  if (stream->final_size >= sizeof(request_line)) {
    return AI_QUIC_ERROR;
  }

  copy_len = (size_t)stream->final_size;
  memcpy(request_line, stream->recv_data, copy_len);
  request_line[copy_len] = '\0';
  if (sscanf(request_line, "GET %511s", stream->request_path) != 1) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "invalid request");
    return AI_QUIC_ERROR;
  }
  stream->request_parsed = 1;
  if (stream->app_consumed < stream->final_size &&
      ai_quic_conn_on_app_consumed(conn, stream, stream->final_size - stream->app_consumed) !=
          AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  return ai_quic_conn_prepare_response_stream(conn, stream, www_root);
}

static ai_quic_result_t ai_quic_conn_process_client_stream(ai_quic_conn_impl_t *conn,
                                                           ai_quic_stream_state_t *stream) {
  uint64_t ready_bytes;

  if (conn == NULL || stream == NULL || stream->output_path[0] == '\0') {
    return AI_QUIC_ERROR;
  }

  ready_bytes = stream->recv_contiguous_end - stream->file_written_offset;
  if (ready_bytes > 0u) {
    if (ai_quic_conn_append_file(stream->output_path,
                                 stream->recv_data + (size_t)stream->file_written_offset,
                                 (size_t)ready_bytes) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "download append failed");
      return AI_QUIC_ERROR;
    }
    if (ai_quic_conn_on_app_consumed(conn, stream, ready_bytes) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    stream->file_written_offset += ready_bytes;
  }

  if (stream->recv_fin && stream->final_size_known &&
      stream->file_written_offset == stream->final_size && !stream->download_finished) {
    stream->download_finished = 1;
    conn->completed_request_streams += 1u;
  }
  return AI_QUIC_OK;
}

static void ai_quic_conn_count_blocked_streams(const ai_quic_conn_impl_t *conn,
                                               size_t *conn_blocked_streams,
                                               size_t *stream_blocked_streams) {
  uint64_t total_sent;
  size_t i;

  if (conn_blocked_streams != NULL) {
    *conn_blocked_streams = 0u;
  }
  if (stream_blocked_streams != NULL) {
    *stream_blocked_streams = 0u;
  }
  if (conn == NULL) {
    return;
  }

  total_sent = ai_quic_conn_total_sent_bytes(conn);
  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++i) {
    const ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    size_t remaining;
    uint64_t stream_credit;
    uint64_t conn_credit;

    if (!stream->in_use || stream->resend_pending) {
      continue;
    }

    remaining = stream->send_data_len > stream->send_offset
                    ? stream->send_data_len - (size_t)stream->send_offset
                    : 0u;
    if (remaining == 0u && (!stream->send_fin_requested || stream->send_fin_sent)) {
      continue;
    }

    stream_credit = stream->flow.send_limit > stream->send_offset
                        ? stream->flow.send_limit - stream->send_offset
                        : 0u;
    conn_credit = conn->conn_flow.send_limit > total_sent
                      ? conn->conn_flow.send_limit - total_sent
                      : 0u;
    if (conn_credit == 0u) {
      if (conn_blocked_streams != NULL) {
        *conn_blocked_streams += 1u;
      }
      continue;
    }
    if (stream_credit == 0u && stream_blocked_streams != NULL) {
      *stream_blocked_streams += 1u;
    }
  }
}

static void ai_quic_conn_log_flush_summary(ai_quic_conn_impl_t *conn,
                                           const ai_quic_app_flush_stats_t *stats) {
  int should_log;

  if (conn == NULL || stats == NULL || !conn->is_server || stats->packets == 0u) {
    return;
  }

  conn->app_flush_count += 1u;
  should_log = conn->app_flush_count <= 16u ||
               (conn->app_flush_count % 64u) == 0u ||
               stats->control_frames > 0u ||
               stats->retransmit_frames > 0u ||
               stats->conn_blocked_streams > 0u ||
               stats->stream_blocked_streams > 0u;
  if (!should_log) {
    return;
  }

  ai_quic_log_write(
      AI_QUIC_LOG_INFO,
      "conn_io",
      "app flush #%llu packets=%zu control=%zu retransmit=%zu new=%zu conn_blocked=%zu stream_blocked=%zu conn_send=%llu/%llu peer_blocked_recent=%d",
      (unsigned long long)conn->app_flush_count,
      stats->packets,
      stats->control_frames,
      stats->retransmit_frames,
      stats->new_data_frames,
      stats->conn_blocked_streams,
      stats->stream_blocked_streams,
      (unsigned long long)ai_quic_conn_total_sent_bytes(conn),
      (unsigned long long)conn->conn_flow.send_limit,
      conn->last_peer_data_blocked_ms != 0u);
}

static size_t ai_quic_conn_append_control_frames(ai_quic_conn_impl_t *conn,
                                                 ai_quic_packet_t *packet) {
  ai_quic_packet_number_space_t *app_space;
  size_t i;
  size_t added;

  if (conn == NULL || packet == NULL) {
    return 0u;
  }

  added = 0u;
  app_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_APP_DATA);
  if (packet->frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET && app_space->ack_needed &&
      app_space->largest_received_packet_number != UINT64_MAX) {
    ai_quic_build_ack_frame(app_space, &packet->frames[packet->frame_count++]);
    app_space->ack_needed = 0;
    added += 1u;
  }
  if (packet->frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET && conn->conn_flow.update_pending) {
    ai_quic_frame_t *frame = &packet->frames[packet->frame_count++];
    frame->type = AI_QUIC_FRAME_MAX_DATA;
    frame->payload.max_data.maximum_data = conn->conn_flow.recv_limit;
    conn->conn_flow.update_pending = 0;
    added += 1u;
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "send MAX_DATA limit=%llu consumed=%llu highest_received=%llu",
                      (unsigned long long)conn->conn_flow.recv_limit,
                      (unsigned long long)conn->conn_flow.recv_consumed,
                      (unsigned long long)conn->conn_flow.highest_received);
  }
  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams) &&
              packet->frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET;
       ++i) {
    ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    if (!stream->in_use || !stream->flow.update_pending) {
      continue;
    }
    packet->frames[packet->frame_count].type = AI_QUIC_FRAME_MAX_STREAM_DATA;
    packet->frames[packet->frame_count].payload.max_stream_data.stream_id = stream->stream_id;
    packet->frames[packet->frame_count].payload.max_stream_data.maximum_stream_data =
        stream->flow.recv_limit;
    packet->frame_count += 1u;
    stream->flow.update_pending = 0;
    added += 1u;
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "send MAX_STREAM_DATA stream=%llu limit=%llu consumed=%llu highest_received=%llu",
                      (unsigned long long)stream->stream_id,
                      (unsigned long long)stream->flow.recv_limit,
                      (unsigned long long)stream->flow.recv_consumed,
                      (unsigned long long)stream->flow.highest_received);
  }
  if (packet->frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET && conn->conn_flow.blocked_pending) {
    ai_quic_frame_t *frame = &packet->frames[packet->frame_count++];
    frame->type = AI_QUIC_FRAME_DATA_BLOCKED;
    frame->payload.data_blocked.limit = conn->conn_flow.last_blocked_limit;
    conn->conn_flow.blocked_pending = 0;
    added += 1u;
  }
  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams) &&
              packet->frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET;
       ++i) {
    ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    if (!stream->in_use || !stream->flow.blocked_pending) {
      continue;
    }
    packet->frames[packet->frame_count].type = AI_QUIC_FRAME_STREAM_DATA_BLOCKED;
    packet->frames[packet->frame_count].payload.stream_data_blocked.stream_id =
        stream->stream_id;
    packet->frames[packet->frame_count].payload.stream_data_blocked.limit =
        stream->flow.last_blocked_limit;
    packet->frame_count += 1u;
    stream->flow.blocked_pending = 0;
    added += 1u;
  }

  return added;
}

static ai_quic_result_t ai_quic_conn_append_stream_frame_common(
    ai_quic_conn_impl_t *conn,
    ai_quic_packet_t *packet,
    ai_quic_stream_state_t *stream,
    size_t index,
    uint64_t offset,
    size_t chunk_len,
    int fin,
    ai_quic_stream_send_mode_t mode) {
  if (conn == NULL || packet == NULL || stream == NULL ||
      packet->frame_count >= AI_QUIC_MAX_FRAMES_PER_PACKET) {
    return AI_QUIC_ERROR;
  }

  packet->frames[packet->frame_count].type = AI_QUIC_FRAME_STREAM;
  packet->frames[packet->frame_count].payload.stream.stream_id = stream->stream_id;
  packet->frames[packet->frame_count].payload.stream.offset = offset;
  packet->frames[packet->frame_count].payload.stream.data_len = chunk_len;
  packet->frames[packet->frame_count].payload.stream.fin = fin;
  if (chunk_len > 0u) {
    memcpy(packet->frames[packet->frame_count].payload.stream.data,
           stream->send_data + offset,
           chunk_len);
  }
  packet->frame_count += 1u;

  if (mode == AI_QUIC_STREAM_SEND_MODE_RETRANSMIT) {
    stream->resend_offset += chunk_len;
    if (stream->resend_offset >= stream->resend_end_offset) {
      if (stream->resend_wrap_offset != 0u) {
        stream->resend_offset = 0u;
        stream->resend_end_offset = stream->resend_wrap_offset;
        stream->resend_wrap_offset = 0u;
      } else {
        stream->resend_pending = 0;
        stream->resend_offset = 0u;
        stream->resend_end_offset = 0u;
      }
    }
  } else {
    stream->send_offset += chunk_len;
    if (fin) {
      stream->send_fin_sent = 1;
      stream->response_finished = stream->response_prepared;
    }
  }

  conn->streams.round_robin_cursor =
      (index + 1u) % AI_QUIC_ARRAY_LEN(conn->streams.streams);
  return AI_QUIC_OK;
}

static int ai_quic_stream_has_priority_tail_retransmission(
    const ai_quic_stream_state_t *stream) {
  uint64_t resend_limit;

  if (stream == NULL || !stream->resend_pending) {
    return 0;
  }
  if (stream->send_fin_sent) {
    return 1;
  }
  resend_limit = stream->send_offset;
  return stream->send_fin_requested && resend_limit >= (uint64_t)stream->send_data_len;
}

static int ai_quic_conn_has_pending_new_stream_data(const ai_quic_conn_impl_t *conn) {
  size_t i;

  if (conn == NULL) {
    return 0;
  }

  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++i) {
    const ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    size_t remaining;

    if (!stream->in_use) {
      continue;
    }

    remaining = stream->send_data_len > stream->send_offset
                    ? stream->send_data_len - (size_t)stream->send_offset
                    : 0u;
    if (remaining > 0u) {
      return 1;
    }
    if (stream->send_fin_requested && !stream->send_fin_sent) {
      return 1;
    }
  }

  return 0;
}

static ai_quic_result_t ai_quic_conn_append_retransmit_stream_frame(
    ai_quic_conn_impl_t *conn,
    ai_quic_packet_t *packet) {
  size_t pass;

  if (conn == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  for (pass = 0; pass < 2u; ++pass) {
    ai_quic_stream_state_t *selected_stream;
    uint64_t selected_limit;
    size_t selected_index;
    size_t index;

    selected_stream = NULL;
    selected_limit = 0u;
    selected_index = 0u;
    for (index = 0; index < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++index) {
      ai_quic_stream_state_t *stream;
      uint64_t resend_limit;

      stream = &conn->streams.streams[index];
      if (!stream->in_use || !stream->resend_pending ||
          ai_quic_stream_has_priority_tail_retransmission(stream) != (int)(pass == 0u)) {
        continue;
      }

      resend_limit = stream->resend_end_offset;
      if (resend_limit == 0u) {
        resend_limit = stream->send_fin_sent ? (uint64_t)stream->send_data_len
                                             : stream->send_offset;
      }
      if (stream->resend_offset >= resend_limit) {
        if (stream->resend_wrap_offset != 0u) {
          stream->resend_offset = 0u;
          stream->resend_end_offset = stream->resend_wrap_offset;
          stream->resend_wrap_offset = 0u;
        } else {
          stream->resend_pending = 0;
          stream->resend_offset = 0u;
          stream->resend_end_offset = 0u;
        }
        continue;
      }

      if (selected_stream == NULL || resend_limit > selected_limit ||
          (resend_limit == selected_limit &&
           stream->stream_id > selected_stream->stream_id)) {
        selected_stream = stream;
        selected_limit = resend_limit;
        selected_index = index;
      }
    }

    if (selected_stream != NULL) {
      size_t remaining;
      size_t chunk_len;
      int fin;

      remaining = (size_t)(selected_limit - selected_stream->resend_offset);
      chunk_len = remaining > AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN
                      ? AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN
                      : remaining;
      fin = selected_stream->send_fin_sent &&
            selected_stream->send_fin_requested &&
            selected_stream->resend_offset + chunk_len == selected_stream->send_data_len;
      return ai_quic_conn_append_stream_frame_common(conn,
                                                     packet,
                                                     selected_stream,
                                                     selected_index,
                                                     selected_stream->resend_offset,
                                                     chunk_len,
                                                     fin,
                                                     AI_QUIC_STREAM_SEND_MODE_RETRANSMIT);
    }
  }

  return AI_QUIC_ERROR;
}

static ai_quic_result_t ai_quic_conn_append_new_stream_frame(ai_quic_conn_impl_t *conn,
                                                             ai_quic_packet_t *packet) {
  size_t attempts;
  size_t start_index;
  uint64_t total_sent;

  if (conn == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  start_index = conn->streams.round_robin_cursor;
  total_sent = ai_quic_conn_total_sent_bytes(conn);
  for (attempts = 0; attempts < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++attempts) {
    ai_quic_stream_state_t *stream;
    uint64_t stream_credit;
    uint64_t conn_credit;
    size_t remaining;
    size_t chunk_len;
    int fin;
    size_t index;

    index = (start_index + attempts) % AI_QUIC_ARRAY_LEN(conn->streams.streams);
    stream = &conn->streams.streams[index];
    if (!stream->in_use || stream->resend_pending ||
        (stream->send_fin_sent && !stream->resend_pending)) {
      continue;
    }

    remaining = stream->send_data_len > stream->send_offset
                    ? stream->send_data_len - (size_t)stream->send_offset
                    : 0u;
    if (remaining == 0u && (!stream->send_fin_requested || stream->send_fin_sent)) {
      continue;
    }

    stream_credit = stream->flow.send_limit > stream->send_offset
                        ? stream->flow.send_limit - stream->send_offset
                        : 0u;
    conn_credit = conn->conn_flow.send_limit > total_sent
                      ? conn->conn_flow.send_limit - total_sent
                      : 0u;
    if (conn_credit == 0u) {
      if (conn->conn_flow.last_blocked_limit != conn->conn_flow.send_limit) {
        conn->conn_flow.blocked_pending = 1;
        conn->conn_flow.last_blocked_limit = conn->conn_flow.send_limit;
      }
      continue;
    }
    if (stream_credit == 0u) {
      if (stream->flow.last_blocked_limit != stream->flow.send_limit) {
        stream->flow.blocked_pending = 1;
        stream->flow.last_blocked_limit = stream->flow.send_limit;
      }
      continue;
    }

    chunk_len = remaining;
    if ((uint64_t)chunk_len > stream_credit) {
      chunk_len = (size_t)stream_credit;
    }
    if ((uint64_t)chunk_len > conn_credit) {
      chunk_len = (size_t)conn_credit;
    }
    if (chunk_len > AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN) {
      chunk_len = AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN;
    }
    fin = stream->send_fin_requested && stream->send_offset + chunk_len == stream->send_data_len;
    if (remaining == 0u) {
      chunk_len = 0u;
      fin = stream->send_fin_requested;
    }

    return ai_quic_conn_append_stream_frame_common(conn,
                                                   packet,
                                                   stream,
                                                   index,
                                                   stream->send_offset,
                                                   chunk_len,
                                                   fin,
                                                   AI_QUIC_STREAM_SEND_MODE_NEW_DATA);
  }

  return AI_QUIC_ERROR;
}

static void ai_quic_conn_schedule_stream_retransmissions(ai_quic_conn_impl_t *conn) {
  size_t i;

  if (conn == NULL) {
    return;
  }

  for (i = 0; i < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++i) {
    ai_quic_stream_state_t *stream = &conn->streams.streams[i];
    uint64_t resend_limit;
    uint64_t tail_start;
    if (!stream->in_use || stream->send_offset == 0u) {
      continue;
    }
    resend_limit = stream->send_fin_sent ? (uint64_t)stream->send_data_len : stream->send_offset;
    if (resend_limit == 0u) {
      continue;
    }
    if (!stream->resend_pending) {
      stream->resend_pending = 1;
      stream->resend_end_offset = resend_limit;
      stream->resend_wrap_offset = 0u;
      if (stream->send_fin_sent) {
        tail_start = ai_quic_stream_tail_resend_start(resend_limit);
        stream->resend_offset = tail_start;
        stream->resend_wrap_offset = tail_start;
      } else {
        stream->resend_offset = 0u;
      }
      continue;
    }
    if (resend_limit > stream->resend_end_offset) {
      stream->resend_end_offset = resend_limit;
    }
  }
}

static ai_quic_result_t ai_quic_conn_flush_app_data(ai_quic_conn_impl_t *conn,
                                                    uint64_t now_ms,
                                                    ai_quic_pending_datagram_t *pending,
                                                    size_t *pending_count,
                                                    size_t pending_capacity) {
  size_t bursts;
  size_t packet_budget;
  size_t retransmit_budget;
  size_t new_data_budget;
  ai_quic_packet_number_space_t *app_space;
  ai_quic_app_flush_stats_t stats;
  int throttle_new_data;
  int recent_credit_update;
  int retransmit_only_mode;

  if (conn == NULL || pending == NULL || pending_count == NULL || !conn->can_send_1rtt) {
    return AI_QUIC_ERROR;
  }

  memset(&stats, 0, sizeof(stats));
  app_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_APP_DATA);
  if (app_space != NULL && app_space->pto_deadline_ms != 0u &&
      now_ms >= app_space->pto_deadline_ms) {
    ai_quic_conn_schedule_stream_retransmissions(conn);
    app_space->pto_deadline_ms = now_ms + 100u;
  }

  throttle_new_data =
      conn->last_peer_data_blocked_ms != 0u &&
      now_ms >= conn->last_peer_data_blocked_ms &&
      now_ms - conn->last_peer_data_blocked_ms <= AI_QUIC_PEER_BLOCKED_COOLDOWN_MS;
  recent_credit_update =
      ((conn->last_peer_max_data_ms != 0u && now_ms >= conn->last_peer_max_data_ms &&
        now_ms - conn->last_peer_max_data_ms <= AI_QUIC_PEER_CREDIT_UPDATE_COOLDOWN_MS) ||
       (conn->last_peer_stream_max_data_ms != 0u &&
        now_ms >= conn->last_peer_stream_max_data_ms &&
        now_ms - conn->last_peer_stream_max_data_ms <= AI_QUIC_PEER_CREDIT_UPDATE_COOLDOWN_MS));
  retransmit_only_mode = !ai_quic_conn_has_pending_new_stream_data(conn);
  retransmit_budget = recent_credit_update ? 0u : AI_QUIC_APP_DATA_RETRANSMIT_BUDGET;
  new_data_budget = throttle_new_data ? AI_QUIC_APP_DATA_THROTTLED_NEW_DATA_BUDGET
                                      : AI_QUIC_APP_DATA_NEW_DATA_BUDGET;
  if (recent_credit_update && new_data_budget < AI_QUIC_APP_DATA_PACKET_BUDGET) {
    new_data_budget = AI_QUIC_APP_DATA_PACKET_BUDGET;
  }
  packet_budget = AI_QUIC_APP_DATA_PACKET_BUDGET;
  if (retransmit_only_mode) {
    packet_budget = AI_QUIC_APP_DATA_RETRANSMIT_ONLY_PACKET_BUDGET;
    retransmit_budget = AI_QUIC_APP_DATA_RETRANSMIT_ONLY_BUDGET;
    new_data_budget = 0u;
  }

  for (bursts = 0; bursts < packet_budget && *pending_count < pending_capacity;
       ++bursts) {
    ai_quic_packet_t packet;
    size_t added_control;
    int have_frames;

    if (!conn->is_server && !conn->handshake_confirmed) {
      break;
    }

    ai_quic_packet_init(&packet, AI_QUIC_PACKET_TYPE_ONE_RTT, conn, AI_QUIC_PN_SPACE_APP_DATA);
    have_frames = 0;
    added_control = ai_quic_conn_append_control_frames(conn, &packet);
    if (added_control > 0u) {
      stats.control_frames += added_control;
      have_frames = 1;
    }
    if (packet.frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET && retransmit_budget > 0u &&
        ai_quic_conn_append_retransmit_stream_frame(conn, &packet) == AI_QUIC_OK) {
      retransmit_budget -= 1u;
      stats.retransmit_frames += 1u;
      have_frames = 1;
    } else if (packet.frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET && new_data_budget > 0u &&
               ai_quic_conn_append_new_stream_frame(conn, &packet) == AI_QUIC_OK) {
      new_data_budget -= 1u;
      stats.new_data_frames += 1u;
      have_frames = 1;
    }
    if (!have_frames) {
      break;
    }

    if (ai_quic_conn_push_datagram(conn,
                                   &packet,
                                   1u,
                                   pending,
                                   pending_count,
                                   pending_capacity) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    stats.packets += 1u;
  }
  ai_quic_conn_count_blocked_streams(conn,
                                     &stats.conn_blocked_streams,
                                     &stats.stream_blocked_streams);
  ai_quic_conn_log_flush_summary(conn, &stats);
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_conn_flush_pending(ai_quic_conn_impl_t *conn,
                                            uint64_t now_ms,
                                            ai_quic_pending_datagram_t *pending,
                                            size_t *pending_count,
                                            size_t pending_capacity,
                                            const char *www_root) {
  ai_quic_tls_event_t event;
  ai_quic_packet_t packets[2];
  size_t packet_count;
  int have_initial_ack;
  size_t pending_before;
  ai_quic_packet_number_space_t *initial_space;
  ai_quic_packet_number_space_t *handshake_space;
  ai_quic_packet_number_space_t *app_space;

  if (conn == NULL || pending == NULL || pending_count == NULL) {
    return AI_QUIC_ERROR;
  }

  initial_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_INITIAL);
  handshake_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_HANDSHAKE);
  app_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_APP_DATA);
  pending_before = *pending_count;
  while (ai_quic_tls_session_poll_event(conn->tls_session, &event) == AI_QUIC_OK) {
    if (!conn->is_server) {
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "conn_io",
                        "poll tls event type=%s level=%d len=%zu pending=%zu hs_ack=%d init_ack=%d can_send_1rtt=%d",
                        ai_quic_tls_event_name(event.type),
                        (int)event.level,
                        event.crypto_data_len,
                        *pending_count,
                        handshake_space->ack_needed,
                        initial_space->ack_needed,
                        conn->can_send_1rtt);
    }
    if (event.type == AI_QUIC_TLS_EVENT_INSTALL_HANDSHAKE_KEYS) {
      ai_quic_pn_space_mark_key_installed(ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_HANDSHAKE));
      ai_quic_qlog_write_key_value(conn->qlog,
                                   now_ms,
                                   "security",
                                   "key_updated",
                                   "key_type",
                                   "handshake");
      continue;
    }
    if (event.type == AI_QUIC_TLS_EVENT_INSTALL_ONE_RTT_KEYS) {
      ai_quic_pn_space_mark_key_installed(ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_APP_DATA));
      conn->can_send_1rtt = 1;
      conn->state = AI_QUIC_CONN_STATE_ACTIVE;
      ai_quic_qlog_write_key_value(conn->qlog,
                                   now_ms,
                                   "security",
                                   "key_updated",
                                   "key_type",
                                   "1rtt");
      continue;
    }
    if (event.type == AI_QUIC_TLS_EVENT_HANDSHAKE_COMPLETE) {
      conn->handshake_completed = 1;
      ai_quic_qlog_write_key_value(conn->qlog,
                                   now_ms,
                                   "connectivity",
                                   "connection_state_updated",
                                   "state",
                                   "handshake_completed");
      continue;
    }
    if (event.type != AI_QUIC_TLS_EVENT_WRITE_CRYPTO) {
      continue;
    }

    packet_count = 0u;
    have_initial_ack = conn->is_server &&
                       event.level == AI_QUIC_ENCRYPTION_HANDSHAKE &&
                       initial_space->largest_received_packet_number != UINT64_MAX;

    if (have_initial_ack) {
      ai_quic_packet_init(&packets[packet_count],
                          AI_QUIC_PACKET_TYPE_INITIAL,
                          conn,
                          AI_QUIC_PN_SPACE_INITIAL);
      ai_quic_build_ack_frame(initial_space, &packets[packet_count].frames[0]);
      packets[packet_count].frame_count = 1u;
      initial_space->ack_needed = 0;
      packet_count += 1u;
    }

    if (ai_quic_conn_build_crypto_packet(conn,
                                         event.level,
                                         event.crypto_data,
                                         event.crypto_data_len,
                                         &packets[packet_count]) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error,
                        sizeof(conn->last_error),
                        "build crypto packet failed level=%d len=%zu",
                        (int)event.level,
                        event.crypto_data_len);
      return AI_QUIC_ERROR;
    }
    packet_count += 1u;

    if (ai_quic_conn_push_datagram(conn,
                                   packets,
                                   packet_count,
                                   pending,
                                   pending_count,
                                   pending_capacity) != AI_QUIC_OK) {
      if (conn->last_error[0] == '\0') {
        ai_quic_set_error(conn->last_error,
                          sizeof(conn->last_error),
                          "push datagram failed packet_count=%zu level=%d",
                          packet_count,
                          (int)event.level);
      }
      return AI_QUIC_ERROR;
    }
  }

  if (*pending_count == pending_before) {
    packet_count = 0u;

    if (initial_space->ack_needed &&
        ai_quic_conn_build_ack_packet(conn,
                                      AI_QUIC_PN_SPACE_INITIAL,
                                      &packets[packet_count]) == AI_QUIC_OK) {
      packet_count += 1u;
    }
    if (handshake_space->ack_needed &&
        ai_quic_conn_build_ack_packet(conn,
                                      AI_QUIC_PN_SPACE_HANDSHAKE,
                                      &packets[packet_count]) == AI_QUIC_OK) {
      packet_count += 1u;
    }
    if (app_space->ack_needed &&
        ai_quic_conn_build_ack_packet(conn,
                                      AI_QUIC_PN_SPACE_APP_DATA,
                                      &packets[packet_count]) == AI_QUIC_OK) {
      packet_count += 1u;
    }

    if (packet_count > 0u &&
        ai_quic_conn_push_datagram(conn,
                                   packets,
                                   packet_count,
                                   pending,
                                   pending_count,
                                   pending_capacity) != AI_QUIC_OK) {
      if (conn->last_error[0] == '\0') {
        ai_quic_set_error(conn->last_error,
                          sizeof(conn->last_error),
                          "push ack-only datagram failed packet_count=%zu",
                          packet_count);
      }
      return AI_QUIC_ERROR;
    }
  }

  if (!conn->is_server && *pending_count == pending_before &&
      (initial_space->ack_needed || handshake_space->ack_needed)) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "no outgoing datagram after flush init_ack=%d hs_ack=%d init_largest=%llu hs_largest=%llu handshake_complete=%d can_send_1rtt=%d",
                      initial_space->ack_needed,
                      handshake_space->ack_needed,
                      (unsigned long long)initial_space->largest_received_packet_number,
                      (unsigned long long)handshake_space->largest_received_packet_number,
                      conn->handshake_completed,
                      conn->can_send_1rtt);
  }

  if (conn->should_send_handshake_done) {
    conn->should_send_handshake_done = 0;
    if (ai_quic_conn_send_handshake_done(conn,
                                         pending,
                                         pending_count,
                                         pending_capacity) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
  }

  if (conn->can_send_1rtt &&
      ai_quic_conn_flush_app_data(conn, now_ms, pending, pending_count, pending_capacity) !=
          AI_QUIC_OK &&
      conn->last_error[0] != '\0') {
    return AI_QUIC_ERROR;
  }

  (void)now_ms;
  (void)www_root;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_conn_init_transport(ai_quic_conn_impl_t *conn,
                                             ai_quic_tls_ctx_t *tls_ctx,
                                             ai_quic_qlog_writer_t *qlog,
                                             const char *alpn,
                                             const char *keylog_path) {
  if (conn == NULL || tls_ctx == NULL) {
    return AI_QUIC_ERROR;
  }

  conn->qlog = qlog;
  conn->tls_session = ai_quic_tls_session_create(
      tls_ctx, conn->is_server, alpn, keylog_path);
  if (conn->tls_session == NULL) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "tls session create failed");
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_conn_start_client(ai_quic_conn_impl_t *conn,
                                           const char *authority,
                                           const char *request_path) {
  uint8_t seed[AI_QUIC_MAX_CID_LEN];

  if (conn == NULL) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_random_cid(&conn->local_cid, 8u) != AI_QUIC_OK ||
      ai_quic_random_cid(&conn->original_destination_cid, 8u) != AI_QUIC_OK ||
      !ai_quic_cid_copy(&conn->peer_cid, &conn->original_destination_cid)) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "cid setup failed");
    return AI_QUIC_ERROR;
  }

  conn->state = AI_QUIC_CONN_STATE_HANDSHAKING;
  conn->local_transport_params.initial_source_connection_id = conn->local_cid;
  conn->local_transport_params.has_original_destination_connection_id = 0;
  ai_quic_pn_space_mark_key_installed(ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_INITIAL));
  conn->conn_flow.initial_window = conn->local_transport_params.initial_max_data;
  conn->conn_flow.recv_limit = conn->local_transport_params.initial_max_data;
  ai_quic_flow_controller_set_send_limit(&conn->conn_flow,
                                         conn->peer_transport_params.initial_max_data);

  memcpy(seed, conn->original_destination_cid.bytes, conn->original_destination_cid.len);
  ai_quic_qlog_write_key_value(conn->qlog,
                               ai_quic_now_ms(),
                               "connectivity",
                               "connection_started",
                               "authority",
                               authority != NULL ? authority : "server");
  (void)seed;

  if (ai_quic_tls_session_start(conn->tls_session,
                                &conn->local_transport_params,
                                authority) !=
      AI_QUIC_OK) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "tls start failed");
    return AI_QUIC_ERROR;
  }
  (void)request_path;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_conn_queue_request(ai_quic_conn_impl_t *conn,
                                            const char *request_path,
                                            const char *downloads_root) {
  ai_quic_stream_state_t *stream;
  char request_line[AI_QUIC_MAX_FRAME_PAYLOAD_LEN];
  char filename[256];
  char output_path[AI_QUIC_MAX_STREAM_OUTPUT_PATH_LEN];
  int written;

  if (conn == NULL || request_path == NULL || request_path[0] == '\0') {
    return AI_QUIC_ERROR;
  }
  if (conn->streams.next_local_bidi_stream_id / 4u >=
      conn->peer_transport_params.initial_max_streams_bidi) {
    return AI_QUIC_ERROR;
  }

  output_path[0] = '\0';
  if (downloads_root != NULL) {
    if (!ai_quic_path_extract_filename(request_path, filename, sizeof(filename))) {
      return AI_QUIC_ERROR;
    }
    snprintf(output_path, sizeof(output_path), "%s/%s", downloads_root, filename);
    if (ai_quic_fs_write_binary_file(output_path, NULL, 0u) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
  }

  stream = ai_quic_stream_manager_open_local_bidi(
      &conn->streams,
      ai_quic_conn_stream_send_limit(conn, 1),
      ai_quic_conn_stream_recv_limit(conn, 1));
  if (stream == NULL) {
    return AI_QUIC_ERROR;
  }

  written = snprintf(request_line, sizeof(request_line), "GET %s\r\n", request_path);
  if (written < 0 || (size_t)written >= sizeof(request_line) ||
      ai_quic_stream_prepare_send(stream,
                                  (const uint8_t *)request_line,
                                  (size_t)written,
                                  1,
                                  request_path,
                                  output_path[0] != '\0' ? output_path : NULL) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  conn->total_request_streams += 1u;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_on_crypto(ai_quic_conn_impl_t *conn,
                                               ai_quic_packet_number_space_id_t space_id,
                                               const ai_quic_packet_t *packet,
                                               const ai_quic_crypto_frame_t *frame) {
  ai_quic_crypto_frame_t incoming;

  if (conn == NULL || packet == NULL || frame == NULL) {
    return AI_QUIC_ERROR;
  }

  incoming = *frame;
  if (ai_quic_crypto_stream_accept(ai_quic_conn_space(conn, space_id), &incoming) !=
      AI_QUIC_OK) {
    ai_quic_set_error(conn->last_error,
                      sizeof(conn->last_error),
                      "crypto offset mismatch expected=%llu got=%llu len=%zu",
                      (unsigned long long)ai_quic_conn_space(conn, space_id)
                          ->crypto_stream.rx_next_offset,
                      (unsigned long long)frame->offset,
                      frame->data_len);
    return AI_QUIC_ERROR;
  }

  if (incoming.data_len > 0u &&
      ai_quic_tls_session_provide_crypto(conn->tls_session,
                                         ai_quic_space_to_level(space_id),
                                         incoming.data,
                                         incoming.data_len) != AI_QUIC_OK) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "tls provide crypto failed");
    return AI_QUIC_ERROR;
  }

  conn->peer_transport_params = *ai_quic_tls_session_peer_transport_params(conn->tls_session);
  if (!conn->is_server && space_id == AI_QUIC_PN_SPACE_HANDSHAKE &&
      !conn->peer_transport_params_validated) {
    if (ai_quic_transport_params_validate_client(&conn->peer_transport_params,
                                                 &conn->original_destination_cid,
                                                 &packet->header.scid) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "client TP validation failed");
      return AI_QUIC_ERROR;
    }
    conn->peer_transport_params_validated = 1;
    ai_quic_conn_apply_peer_transport_params(conn);
  }
  if (conn->is_server && space_id == AI_QUIC_PN_SPACE_INITIAL &&
      !conn->peer_transport_params_validated) {
    if (ai_quic_transport_params_validate_server(&conn->peer_transport_params,
                                                 &packet->header.scid) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "server TP validation failed");
      return AI_QUIC_ERROR;
    }
    conn->peer_transport_params_validated = 1;
    ai_quic_conn_apply_peer_transport_params(conn);
  }

  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_on_stream(ai_quic_conn_impl_t *conn,
                                               const ai_quic_stream_frame_t *frame,
                                               const char *www_root) {
  ai_quic_stream_state_t *stream;
  uint64_t newly_received;
  size_t i;

  if (conn == NULL || frame == NULL) {
    return AI_QUIC_ERROR;
  }

  if (!conn->can_send_1rtt) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "received STREAM before 1-RTT ready");
    return AI_QUIC_ERROR;
  }

  stream = ai_quic_stream_manager_find(&conn->streams, frame->stream_id);
  if (stream == NULL) {
    if (frame->stream_id / 4u >= conn->local_transport_params.initial_max_streams_bidi) {
      return AI_QUIC_ERROR;
    }
    stream = ai_quic_stream_manager_get_or_create_remote_bidi(
        &conn->streams,
        frame->stream_id,
        ai_quic_conn_stream_send_limit(conn, 0),
        ai_quic_conn_stream_recv_limit(conn, 0));
  }
  if (stream == NULL) {
    return AI_QUIC_ERROR;
  }

  newly_received = 0u;
  for (i = 0; i < frame->data_len; ++i) {
    uint64_t absolute = frame->offset + i;
    if (absolute >= stream->recv_capacity || stream->recv_map[absolute] == 0u) {
      newly_received += 1u;
    }
  }
  if (conn->conn_flow.highest_received + newly_received > conn->conn_flow.recv_limit) {
    ai_quic_set_error(conn->last_error,
                      sizeof(conn->last_error),
                      "conn recv_limit exceeded stream=%llu frame_offset=%llu len=%zu newly_received=%llu conn_received=%llu conn_limit=%llu",
                      (unsigned long long)frame->stream_id,
                      (unsigned long long)frame->offset,
                      frame->data_len,
                      (unsigned long long)newly_received,
                      (unsigned long long)conn->conn_flow.highest_received,
                      (unsigned long long)conn->conn_flow.recv_limit);
    return AI_QUIC_ERROR;
  }

  if (ai_quic_stream_on_receive(stream, frame) != AI_QUIC_OK) {
    ai_quic_set_error(conn->last_error,
                      sizeof(conn->last_error),
                      "stream receive failed: %s",
                      stream->last_error[0] != '\0' ? stream->last_error : "unknown");
    return AI_QUIC_ERROR;
  }
  conn->conn_flow.highest_received += newly_received;

  if (conn->is_server) {
    if (ai_quic_conn_process_server_stream(conn, stream, www_root) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
  } else {
    if (ai_quic_conn_process_client_stream(conn, stream) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_conn_on_packet(ai_quic_conn_impl_t *conn,
                                        const ai_quic_packet_t *packet,
                                        uint64_t now_ms,
                                        ai_quic_pending_datagram_t *pending,
                                        size_t *pending_count,
                                        size_t pending_capacity,
                                        const char *www_root,
                                        const char *downloads_root) {
  ai_quic_packet_number_space_id_t space_id;
  int ack_eliciting;
  size_t i;

  if (conn == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  conn->bytes_received += packet->header.packet_length;
  conn->last_activity_ms = now_ms;

  if (!conn->is_server && packet->header.type == AI_QUIC_PACKET_TYPE_INITIAL &&
      !conn->saw_first_server_initial) {
    conn->peer_cid = packet->header.scid;
    conn->saw_first_server_initial = 1;
  }

  if (conn->is_server && conn->state == AI_QUIC_CONN_STATE_PRE_VALIDATION) {
    conn->state = AI_QUIC_CONN_STATE_HANDSHAKING;
    conn->peer_cid = packet->header.scid;
    conn->original_destination_cid = packet->header.dcid;
    ai_quic_random_cid(&conn->local_cid, 8u);
    conn->local_transport_params.initial_source_connection_id = conn->local_cid;
    conn->local_transport_params.original_destination_connection_id =
        conn->original_destination_cid;
    conn->local_transport_params.has_original_destination_connection_id = 1;
    ai_quic_pn_space_mark_key_installed(ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_INITIAL));
    if (ai_quic_tls_session_start(conn->tls_session,
                                  &conn->local_transport_params,
                                  NULL) !=
        AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "server tls start failed");
      return AI_QUIC_ERROR;
    }
  }

  if (packet->header.type == AI_QUIC_PACKET_TYPE_ONE_RTT && !conn->can_send_1rtt) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "received 1-RTT before ready");
    return AI_QUIC_ERROR;
  }

  if (packet->header.type == AI_QUIC_PACKET_TYPE_INITIAL) {
    space_id = AI_QUIC_PN_SPACE_INITIAL;
  } else if (packet->header.type == AI_QUIC_PACKET_TYPE_HANDSHAKE) {
    space_id = AI_QUIC_PN_SPACE_HANDSHAKE;
  } else {
    space_id = AI_QUIC_PN_SPACE_APP_DATA;
  }

  ai_quic_pn_space_on_packet_received(ai_quic_conn_space(conn, space_id),
                                      packet->header.packet_number);
  ai_quic_timer_on_space_active(conn, space_id, now_ms);

  ack_eliciting = 0;
  for (i = 0; i < packet->frame_count; ++i) {
    const ai_quic_frame_t *frame = &packet->frames[i];
    if (frame->type != AI_QUIC_FRAME_ACK && frame->type != AI_QUIC_FRAME_PADDING) {
      ack_eliciting = 1;
    }
    switch (frame->type) {
      case AI_QUIC_FRAME_ACK:
        ai_quic_pn_space_on_ack(ai_quic_conn_space(conn, space_id),
                                frame->payload.ack.largest_acked);
        break;
      case AI_QUIC_FRAME_CRYPTO:
        if (ai_quic_conn_on_crypto(conn, space_id, packet, &frame->payload.crypto) !=
            AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        break;
      case AI_QUIC_FRAME_STREAM:
        if (ai_quic_conn_on_stream(conn, &frame->payload.stream, www_root) !=
            AI_QUIC_OK) {
          return AI_QUIC_ERROR;
        }
        break;
      case AI_QUIC_FRAME_HANDSHAKE_DONE:
        conn->handshake_confirmed = 1;
        conn->address_validated = 1;
        conn->state = AI_QUIC_CONN_STATE_ACTIVE;
        ai_quic_loss_discard_space(conn, AI_QUIC_PN_SPACE_HANDSHAKE);
        ai_quic_qlog_write_key_value(conn->qlog,
                                     now_ms,
                                     "connectivity",
                                     "connection_state_updated",
                                     "state",
                                     "handshake_confirmed");
        break;
      case AI_QUIC_FRAME_MAX_DATA:
        ai_quic_log_write(AI_QUIC_LOG_INFO,
                          "conn_io",
                          "recv MAX_DATA old=%llu new=%llu",
                          (unsigned long long)conn->conn_flow.send_limit,
                          (unsigned long long)frame->payload.max_data.maximum_data);
        ai_quic_flow_controller_set_send_limit(&conn->conn_flow,
                                               frame->payload.max_data.maximum_data);
        conn->last_peer_max_data_ms = now_ms;
        conn->last_peer_data_blocked_ms = 0u;
        break;
      case AI_QUIC_FRAME_MAX_STREAM_DATA: {
        ai_quic_stream_state_t *stream =
            ai_quic_stream_manager_find(&conn->streams,
                                        frame->payload.max_stream_data.stream_id);
        if (stream != NULL) {
          ai_quic_log_write(AI_QUIC_LOG_INFO,
                            "conn_io",
                            "recv MAX_STREAM_DATA stream=%llu old=%llu new=%llu",
                            (unsigned long long)stream->stream_id,
                            (unsigned long long)stream->flow.send_limit,
                            (unsigned long long)frame->payload.max_stream_data.maximum_stream_data);
          ai_quic_flow_controller_set_send_limit(
              &stream->flow,
              frame->payload.max_stream_data.maximum_stream_data);
          conn->last_peer_stream_max_data_ms = now_ms;
          conn->last_peer_data_blocked_ms = 0u;
        }
        break;
      }
      case AI_QUIC_FRAME_DATA_BLOCKED:
        conn->last_peer_data_blocked_ms = now_ms;
        ai_quic_log_write(AI_QUIC_LOG_INFO,
                          "conn_io",
                          "recv DATA_BLOCKED limit=%llu",
                          (unsigned long long)frame->payload.data_blocked.limit);
        break;
      case AI_QUIC_FRAME_STREAM_DATA_BLOCKED:
        conn->last_peer_data_blocked_ms = now_ms;
        ai_quic_log_write(AI_QUIC_LOG_INFO,
                          "conn_io",
                          "recv STREAM_DATA_BLOCKED stream=%llu limit=%llu",
                          (unsigned long long)frame->payload.stream_data_blocked.stream_id,
                          (unsigned long long)frame->payload.stream_data_blocked.limit);
        break;
      case AI_QUIC_FRAME_CONNECTION_CLOSE:
        conn->state = AI_QUIC_CONN_STATE_CLOSING;
        break;
      case AI_QUIC_FRAME_PADDING:
      default:
        break;
    }
  }
  if (ack_eliciting) {
    ai_quic_conn_space(conn, space_id)->ack_needed = 1;
  }

  if (!conn->is_server) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "processed %s packet pn=%llu frame_count=%zu init_ack=%d hs_ack=%d hs_complete=%d can_send_1rtt=%d",
                      ai_quic_space_name(space_id),
                      (unsigned long long)packet->header.packet_number,
                      packet->frame_count,
                      ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_INITIAL)->ack_needed,
                      ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_HANDSHAKE)->ack_needed,
                      conn->handshake_completed,
                      conn->can_send_1rtt);
  }

  if (conn->is_server && packet->header.type == AI_QUIC_PACKET_TYPE_HANDSHAKE) {
    conn->address_validated = 1;
    conn->can_send_1rtt = 1;
    conn->handshake_completed = ai_quic_tls_session_handshake_complete(conn->tls_session);
    conn->should_send_handshake_done = conn->handshake_completed;
    ai_quic_loss_discard_space(conn, AI_QUIC_PN_SPACE_INITIAL);
  }

  if (!conn->is_server && packet->header.type == AI_QUIC_PACKET_TYPE_HANDSHAKE &&
      ai_quic_tls_session_handshake_complete(conn->tls_session)) {
    conn->handshake_completed = 1;
    conn->can_send_1rtt = 1;
  }

  (void)pending;
  (void)pending_count;
  (void)pending_capacity;
  (void)downloads_root;
  return AI_QUIC_OK;
}
