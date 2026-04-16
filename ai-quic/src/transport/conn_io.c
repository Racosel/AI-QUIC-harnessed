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
#define AI_QUIC_PTO_PROBE_PACKET_BUDGET 2u
#define AI_QUIC_PEER_BLOCKED_COOLDOWN_MS 250u
#define AI_QUIC_PEER_CREDIT_UPDATE_COOLDOWN_MS 250u

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
static int ai_quic_conn_has_pending_new_stream_data(const ai_quic_conn_impl_t *conn);
static int ai_quic_conn_has_pending_lost_stream_data(const ai_quic_conn_impl_t *conn);
static uint64_t ai_quic_ack_frame_smallest_acked(const ai_quic_ack_frame_t *ack);
static void ai_quic_conn_on_ack_frame(ai_quic_conn_impl_t *conn,
                                      ai_quic_packet_number_space_id_t space_id,
                                      const ai_quic_ack_frame_t *ack,
                                      uint64_t now_ms);

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

static int ai_quic_flow_crossed_progress_step(uint64_t before, uint64_t after) {
  return before / AI_QUIC_FLOW_LOG_PROGRESS_STEP != after / AI_QUIC_FLOW_LOG_PROGRESS_STEP;
}

static uint64_t ai_quic_flow_update_margin(const ai_quic_flow_controller_t *controller) {
  uint64_t margin;

  if (controller == NULL) {
    return 0u;
  }

  margin = controller->initial_window / 2u;
  margin += AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN;
  return margin;
}

static void ai_quic_conn_maybe_schedule_max_data(ai_quic_conn_impl_t *conn,
                                                 uint64_t stream_id,
                                                 uint64_t trigger_bytes,
                                                 const char *reason) {
  uint64_t recv_floor;
  uint64_t update_margin;
  uint64_t next_limit;
  uint64_t prev_limit;

  if (conn == NULL || conn->conn_flow.initial_window == 0u) {
    return;
  }

  recv_floor = conn->conn_flow.recv_consumed > conn->conn_flow.highest_received
                   ? conn->conn_flow.recv_consumed
                   : conn->conn_flow.highest_received;
  update_margin = ai_quic_flow_update_margin(&conn->conn_flow);
  if (recv_floor + update_margin < conn->conn_flow.recv_limit) {
    return;
  }

  next_limit = recv_floor + conn->conn_flow.initial_window;
  next_limit += AI_QUIC_CONN_RECV_LIMIT_RESERVE;
  if (next_limit <= conn->conn_flow.recv_limit) {
    return;
  }

  prev_limit = conn->conn_flow.recv_limit;
  conn->conn_flow.recv_limit = next_limit;
  conn->conn_flow.update_pending = 1;
  ai_quic_log_write(
      AI_QUIC_LOG_INFO,
      "conn_io",
      "schedule MAX_DATA reason=%s old_limit=%llu new_limit=%llu recv_floor=%llu consumed=%llu highest_received=%llu stream=%llu bytes=%llu margin=%llu reserve=%u",
      reason != NULL ? reason : "unknown",
      (unsigned long long)prev_limit,
      (unsigned long long)conn->conn_flow.recv_limit,
      (unsigned long long)recv_floor,
      (unsigned long long)conn->conn_flow.recv_consumed,
      (unsigned long long)conn->conn_flow.highest_received,
      (unsigned long long)stream_id,
      (unsigned long long)trigger_bytes,
      (unsigned long long)update_margin,
      AI_QUIC_CONN_RECV_LIMIT_RESERVE);
}

static uint64_t ai_quic_ack_frame_smallest_acked(const ai_quic_ack_frame_t *ack) {
  uint64_t range_end;
  uint64_t range_start;
  size_t i;

  if (ack == NULL) {
    return 0u;
  }
  if (ack->largest_acked < ack->first_ack_range) {
    return 0u;
  }

  range_end = ack->largest_acked;
  range_start = range_end - ack->first_ack_range;
  for (i = 0; i < ack->ack_range_count; ++i) {
    if (range_start < ack->ack_ranges[i].gap + 2u) {
      return 0u;
    }
    range_end = range_start - ack->ack_ranges[i].gap - 2u;
    if (range_end < ack->ack_ranges[i].ack_range) {
      return 0u;
    }
    range_start = range_end - ack->ack_ranges[i].ack_range;
  }
  return range_start;
}

static void ai_quic_conn_on_ack_frame(ai_quic_conn_impl_t *conn,
                                      ai_quic_packet_number_space_id_t space_id,
                                      const ai_quic_ack_frame_t *ack,
                                      uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;
  uint64_t smallest_acked;
  uint64_t prev_largest_acked;
  uint32_t prev_pto_count;
  int ack_progress;

  if (conn == NULL || ack == NULL) {
    return;
  }

  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }
  smallest_acked = ai_quic_ack_frame_smallest_acked(ack);
  prev_largest_acked = space->largest_acked_by_peer;
  prev_pto_count = conn->loss_state[space_id].pto_count;

  ai_quic_log_write(
      AI_QUIC_LOG_INFO,
      "conn_io",
      "recv ACK space=%s largest=%llu smallest=%llu first_range=%llu extra_ranges=%zu prev_largest_present=%d latest_sent=%llu bytes_in_flight=%llu pto_deadline=%llu pto_count=%u",
      ai_quic_space_name(space_id),
      (unsigned long long)ack->largest_acked,
      (unsigned long long)smallest_acked,
      (unsigned long long)ack->first_ack_range,
      ack->ack_range_count,
      prev_largest_acked != UINT64_MAX,
      (unsigned long long)conn->loss_state[space_id].latest_sent_packet,
      (unsigned long long)space->bytes_in_flight,
      (unsigned long long)space->pto_deadline_ms,
      conn->loss_state[space_id].pto_count);
  if (prev_largest_acked != UINT64_MAX) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "recv ACK detail space=%s prev_largest=%llu new_largest=%llu ack_age=%llu srtt=%llu latest_rtt=%llu rttvar=%llu",
                      ai_quic_space_name(space_id),
                      (unsigned long long)prev_largest_acked,
                      (unsigned long long)ack->largest_acked,
                      (unsigned long long)(conn->loss_state[space_id].last_ack_ms == 0u
                                               ? 0u
                                               : now_ms - conn->loss_state[space_id].last_ack_ms),
                      (unsigned long long)conn->rtt_state.smoothed_rtt_ms,
                      (unsigned long long)conn->rtt_state.latest_rtt_ms,
                      (unsigned long long)conn->rtt_state.rttvar_ms);
  }

  ai_quic_loss_on_ack_received(conn, space_id, ack, now_ms, &ack_progress);
  if (ack_progress && conn->loss_state[space_id].last_ack_ms == now_ms) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "ack progress reset PTO state space=%s old_pto_count=%u latest_sent=%llu largest_acked=%llu bytes_in_flight=%llu",
                      ai_quic_space_name(space_id),
                      prev_pto_count,
                      (unsigned long long)conn->loss_state[space_id].latest_sent_packet,
                      (unsigned long long)ack->largest_acked,
                      (unsigned long long)space->bytes_in_flight);
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
  size_t packet_offset;
  uint64_t send_time_ms;

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

  send_time_ms = ai_quic_now_ms();
  packet_offset = 0u;
  for (i = 0; i < packet_count; ++i) {
    if (ai_quic_loss_on_packet_sent(conn,
                                    ai_quic_packet_type_to_space(packets[i].header.type),
                                    &packets[i],
                                    packets[i].header.packet_length,
                                    send_time_ms) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error,
                        sizeof(conn->last_error),
                        "recovery tracking failed type=%d pn=%llu",
                        (int)packets[i].header.type,
                        (unsigned long long)packets[i].header.packet_number);
      return AI_QUIC_ERROR;
    }
    ai_quic_qlog_packet(conn,
                        send_time_ms,
                        "packet_sent",
                        &packets[i],
                        packets[i].header.packet_length,
                        pending[*pending_count].bytes + packet_offset,
                        packets[i].header.packet_length);
    packet_offset += packets[i].header.packet_length;
  }

  pending[*pending_count].len = offset;
  *pending_count += 1u;
  conn->bytes_sent += offset;
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
  uint64_t prev_consumed;

  if (conn == NULL || stream == NULL) {
    return AI_QUIC_ERROR;
  }
  if (ai_quic_stream_consume(stream, bytes) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  prev_consumed = conn->conn_flow.recv_consumed;
  conn->conn_flow.recv_consumed += bytes;
  ai_quic_conn_maybe_schedule_max_data(conn, stream->stream_id, bytes, "consume");
  if (ai_quic_flow_crossed_progress_step(prev_consumed, conn->conn_flow.recv_consumed)) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "conn app consumed progress old=%llu new=%llu highest_received=%llu recv_limit=%llu update_pending=%d stream=%llu",
                      (unsigned long long)prev_consumed,
                      (unsigned long long)conn->conn_flow.recv_consumed,
                      (unsigned long long)conn->conn_flow.highest_received,
                      (unsigned long long)conn->conn_flow.recv_limit,
                      conn->conn_flow.update_pending,
                      (unsigned long long)stream->stream_id);
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
  ai_quic_log_write(AI_QUIC_LOG_INFO,
                    "conn_io",
                    "prepared response stream=%llu file=%s bytes=%zu conn_send_limit=%llu stream_send_limit=%llu",
                    (unsigned long long)stream->stream_id,
                    full_path,
                    file_len,
                    (unsigned long long)conn->conn_flow.send_limit,
                    (unsigned long long)stream->flow.send_limit);
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
    uint64_t prev_written = stream->file_written_offset;
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
    if (ai_quic_flow_crossed_progress_step(prev_written, stream->file_written_offset) ||
        (stream->final_size_known && stream->file_written_offset == stream->final_size)) {
      ai_quic_log_write(
          AI_QUIC_LOG_INFO,
          "conn_io",
          "client download progress stream=%llu wrote=%llu/%llu contiguous=%llu highest_received=%llu recv_limit=%llu conn_consumed=%llu conn_recv_limit=%llu",
          (unsigned long long)stream->stream_id,
          (unsigned long long)stream->file_written_offset,
          (unsigned long long)stream->final_size,
          (unsigned long long)stream->recv_contiguous_end,
          (unsigned long long)stream->flow.highest_received,
          (unsigned long long)stream->flow.recv_limit,
          (unsigned long long)conn->conn_flow.recv_consumed,
          (unsigned long long)conn->conn_flow.recv_limit);
    }
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

static size_t ai_quic_conn_append_ping_frame(ai_quic_packet_t *packet) {
  if (packet == NULL || packet->frame_count >= AI_QUIC_MAX_FRAMES_PER_PACKET) {
    return 0u;
  }
  packet->frames[packet->frame_count].type = AI_QUIC_FRAME_PING;
  packet->frame_count += 1u;
  return 1u;
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

  {
    uint64_t prev_send_offset = stream->send_offset;
    uint64_t prev_total_sent = ai_quic_conn_total_sent_bytes(conn);

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

    if (mode == AI_QUIC_STREAM_SEND_MODE_NEW_DATA) {
      stream->send_offset += chunk_len;
      if (fin) {
        stream->send_fin_sent = 1;
        stream->response_finished = stream->response_prepared;
      }
    }

    conn->streams.round_robin_cursor =
        (index + 1u) % AI_QUIC_ARRAY_LEN(conn->streams.streams);
    if (mode == AI_QUIC_STREAM_SEND_MODE_NEW_DATA &&
        (ai_quic_flow_crossed_progress_step(prev_send_offset, stream->send_offset) || fin ||
         chunk_len < AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN)) {
      ai_quic_log_write(
          AI_QUIC_LOG_INFO,
          "conn_io",
          "send stream data stream=%llu offset=%llu chunk=%zu fin=%d send_progress=%llu/%zu stream_credit_rem=%llu conn_send_progress=%llu/%llu",
          (unsigned long long)stream->stream_id,
          (unsigned long long)offset,
          chunk_len,
          fin,
          (unsigned long long)stream->send_offset,
          stream->send_data_len,
          (unsigned long long)(stream->flow.send_limit > stream->send_offset
                                   ? stream->flow.send_limit - stream->send_offset
                                   : 0u),
          (unsigned long long)(prev_total_sent + chunk_len),
          (unsigned long long)conn->conn_flow.send_limit);
    } else if (mode == AI_QUIC_STREAM_SEND_MODE_RETRANSMIT) {
      ai_quic_log_write(
          AI_QUIC_LOG_DEBUG,
          "conn_io",
          "retransmit stream data stream=%llu offset=%llu chunk=%zu fin=%d lost_ranges=%zu fin_pending=%d send_progress=%llu/%zu",
          (unsigned long long)stream->stream_id,
          (unsigned long long)offset,
          chunk_len,
          fin,
          stream->lost_ranges.count,
          stream->lost_fin_pending,
          (unsigned long long)stream->send_offset,
          stream->send_data_len);
    }
  }
  return AI_QUIC_OK;
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

static int ai_quic_conn_has_pending_lost_stream_data(const ai_quic_conn_impl_t *conn) {
  size_t i;

  if (conn == NULL) {
    return 0;
  }

  for (i = 0u; i < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++i) {
    if (conn->streams.streams[i].in_use &&
        ai_quic_stream_has_lost_data(&conn->streams.streams[i])) {
      return 1;
    }
  }
  return 0;
}

static ai_quic_result_t ai_quic_conn_append_retransmit_stream_frame(
    ai_quic_conn_impl_t *conn,
    ai_quic_packet_t *packet) {
  ai_quic_stream_state_t *selected_stream;
  uint64_t selected_start;
  int selected_has_data;
  size_t selected_index;
  size_t index;

  if (conn == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  selected_stream = NULL;
  selected_start = UINT64_MAX;
  selected_has_data = 0;
  selected_index = 0u;
  for (index = 0u; index < AI_QUIC_ARRAY_LEN(conn->streams.streams); ++index) {
    ai_quic_stream_state_t *stream = &conn->streams.streams[index];
    uint64_t candidate_start;
    int candidate_has_data;

    if (!stream->in_use || !ai_quic_stream_has_lost_data(stream)) {
      continue;
    }

    candidate_has_data = stream->lost_ranges.count > 0u;
    candidate_start = candidate_has_data ? stream->lost_ranges.ranges[0].start
                                         : (uint64_t)stream->send_data_len;

    if (selected_stream == NULL ||
        (candidate_has_data && !selected_has_data) ||
        (candidate_has_data == selected_has_data &&
         (candidate_start < selected_start ||
          (candidate_start == selected_start &&
           stream->stream_id < selected_stream->stream_id)))) {
      selected_stream = stream;
      selected_start = candidate_start;
      selected_has_data = candidate_has_data;
      selected_index = index;
    }
  }

  if (selected_stream != NULL) {
    uint64_t offset;
    size_t chunk_len;
    int fin;

    if (!ai_quic_stream_pop_lost_range(selected_stream,
                                       AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN,
                                       &offset,
                                       &chunk_len,
                                       &fin)) {
      return AI_QUIC_ERROR;
    }
    return ai_quic_conn_append_stream_frame_common(conn,
                                                   packet,
                                                   selected_stream,
                                                   selected_index,
                                                   offset,
                                                   chunk_len,
                                                   fin,
                                                   AI_QUIC_STREAM_SEND_MODE_RETRANSMIT);
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
        ai_quic_log_write(
            AI_QUIC_LOG_INFO,
            "conn_io",
            "conn flow blocked stream=%llu total_sent=%llu conn_send_limit=%llu stream_send_offset=%llu stream_send_limit=%llu remaining=%zu",
            (unsigned long long)stream->stream_id,
            (unsigned long long)total_sent,
            (unsigned long long)conn->conn_flow.send_limit,
            (unsigned long long)stream->send_offset,
            (unsigned long long)stream->flow.send_limit,
            remaining);
      }
      continue;
    }
    if (stream_credit == 0u) {
      if (stream->flow.last_blocked_limit != stream->flow.send_limit) {
        stream->flow.blocked_pending = 1;
        stream->flow.last_blocked_limit = stream->flow.send_limit;
        ai_quic_log_write(
            AI_QUIC_LOG_INFO,
            "conn_io",
            "stream flow blocked stream=%llu send_offset=%llu stream_send_limit=%llu conn_credit=%llu remaining=%zu",
            (unsigned long long)stream->stream_id,
            (unsigned long long)stream->send_offset,
            (unsigned long long)stream->flow.send_limit,
            (unsigned long long)conn_credit,
            remaining);
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
    if ((uint64_t)chunk_len < (uint64_t)remaining ||
        chunk_len < AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN) {
      ai_quic_log_write(
          AI_QUIC_LOG_INFO,
          "conn_io",
          "send chunk constrained stream=%llu remaining=%zu chosen=%zu stream_credit=%llu conn_credit=%llu chunk_cap=%u send_offset=%llu total_sent=%llu",
          (unsigned long long)stream->stream_id,
          remaining,
          chunk_len,
          (unsigned long long)stream_credit,
          (unsigned long long)conn_credit,
          AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN,
          (unsigned long long)stream->send_offset,
          (unsigned long long)total_sent);
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

static ai_quic_result_t ai_quic_conn_flush_app_data(ai_quic_conn_impl_t *conn,
                                                    uint64_t now_ms,
                                                    ai_quic_pending_datagram_t *pending,
                                                    size_t *pending_count,
                                                    size_t pending_capacity) {
  size_t bursts;
  size_t packet_budget;
  size_t retransmit_budget;
  size_t new_data_budget;
  size_t pto_probe_budget;
  ai_quic_packet_number_space_t *app_space;
  ai_quic_app_flush_stats_t stats;
  int throttle_new_data;
  int recent_credit_update;
  int retransmit_only_mode;
  int has_new_data;
  int has_lost_data;

  if (conn == NULL || pending == NULL || pending_count == NULL || !conn->can_send_1rtt) {
    return AI_QUIC_ERROR;
  }

  memset(&stats, 0, sizeof(stats));
  app_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_APP_DATA);
  pto_probe_budget = 0u;
  if (app_space != NULL && ai_quic_loss_on_timeout(conn, AI_QUIC_PN_SPACE_APP_DATA, now_ms)) {
    ai_quic_loss_state_t *loss = &conn->loss_state[AI_QUIC_PN_SPACE_APP_DATA];
    pto_probe_budget = AI_QUIC_PTO_PROBE_PACKET_BUDGET;
    ai_quic_log_write(
        AI_QUIC_LOG_INFO,
        "conn_io",
        "pto fire space=app pto_count=%u latest_sent=%llu largest_acked=%llu ack_age=%llu bytes_in_flight=%llu pending_new=%d pending_lost=%d conn_send=%llu/%llu",
        loss->pto_count,
        (unsigned long long)loss->latest_sent_packet,
        (unsigned long long)loss->largest_acked_packet,
        (unsigned long long)(loss->last_ack_ms == 0u || now_ms < loss->last_ack_ms
                                 ? 0u
                                 : now_ms - loss->last_ack_ms),
        (unsigned long long)app_space->bytes_in_flight,
        ai_quic_conn_has_pending_new_stream_data(conn),
        ai_quic_conn_has_pending_lost_stream_data(conn),
        (unsigned long long)ai_quic_conn_total_sent_bytes(conn),
        (unsigned long long)conn->conn_flow.send_limit);
  }

  has_new_data = ai_quic_conn_has_pending_new_stream_data(conn);
  has_lost_data = ai_quic_conn_has_pending_lost_stream_data(conn);
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
  retransmit_only_mode = !has_new_data && has_lost_data;
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
  if (packet_budget < pto_probe_budget) {
    packet_budget = pto_probe_budget;
  }
  if (throttle_new_data || recent_credit_update || retransmit_only_mode) {
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "conn_io",
                      "flush app data budgets throttle_new=%d recent_credit=%d retransmit_only=%d packet_budget=%zu retransmit_budget=%zu new_data_budget=%zu pto_probes=%zu conn_send=%llu/%llu peer_blocked_age=%llu peer_max_data_age=%llu peer_stream_max_age=%llu",
                      throttle_new_data,
                      recent_credit_update,
                      retransmit_only_mode,
                      packet_budget,
                      retransmit_budget,
                      new_data_budget,
                      pto_probe_budget,
                      (unsigned long long)ai_quic_conn_total_sent_bytes(conn),
                      (unsigned long long)conn->conn_flow.send_limit,
                      (unsigned long long)(conn->last_peer_data_blocked_ms == 0u
                                               ? 0u
                                               : now_ms - conn->last_peer_data_blocked_ms),
                      (unsigned long long)(conn->last_peer_max_data_ms == 0u
                                               ? 0u
                                               : now_ms - conn->last_peer_max_data_ms),
                      (unsigned long long)(conn->last_peer_stream_max_data_ms == 0u
                                               ? 0u
                                               : now_ms - conn->last_peer_stream_max_data_ms));
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
    if (packet.frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET && pto_probe_budget > 0u) {
      if (ai_quic_conn_append_retransmit_stream_frame(conn, &packet) == AI_QUIC_OK) {
        pto_probe_budget -= 1u;
        stats.retransmit_frames += 1u;
        have_frames = 1;
        ai_quic_log_write(AI_QUIC_LOG_DEBUG,
                          "conn_io",
                          "pto_probe_pick kind=lost frame_count=%zu",
                          packet.frame_count);
      } else if (ai_quic_conn_append_new_stream_frame(conn, &packet) == AI_QUIC_OK) {
        pto_probe_budget -= 1u;
        stats.new_data_frames += 1u;
        have_frames = 1;
        ai_quic_log_write(AI_QUIC_LOG_DEBUG,
                          "conn_io",
                          "pto_probe_pick kind=new frame_count=%zu",
                          packet.frame_count);
      } else if (ai_quic_conn_append_ping_frame(&packet) > 0u) {
        pto_probe_budget -= 1u;
        stats.control_frames += 1u;
        have_frames = 1;
        ai_quic_log_write(AI_QUIC_LOG_DEBUG,
                          "conn_io",
                          "pto_probe_pick kind=ping frame_count=%zu",
                          packet.frame_count);
      }
    }
    if (!have_frames && packet.frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET &&
        retransmit_budget > 0u &&
        ai_quic_conn_append_retransmit_stream_frame(conn, &packet) == AI_QUIC_OK) {
      retransmit_budget -= 1u;
      stats.retransmit_frames += 1u;
      have_frames = 1;
    } else if (!have_frames && packet.frame_count < AI_QUIC_MAX_FRAMES_PER_PACKET &&
               new_data_budget > 0u &&
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
  ai_quic_conn_maybe_schedule_max_data(conn,
                                       stream->stream_id,
                                       newly_received,
                                       "receive");

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
        ai_quic_conn_on_ack_frame(conn, space_id, &frame->payload.ack, now_ms);
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
                          "recv MAX_DATA old=%llu new=%llu total_sent=%llu blocked_pending=%d",
                          (unsigned long long)conn->conn_flow.send_limit,
                          (unsigned long long)frame->payload.max_data.maximum_data,
                          (unsigned long long)ai_quic_conn_total_sent_bytes(conn),
                          conn->conn_flow.blocked_pending);
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
                            "recv MAX_STREAM_DATA stream=%llu old=%llu new=%llu send_offset=%llu remaining=%zu blocked_pending=%d",
                            (unsigned long long)stream->stream_id,
                            (unsigned long long)stream->flow.send_limit,
                            (unsigned long long)frame->payload.max_stream_data.maximum_stream_data,
                            (unsigned long long)stream->send_offset,
                            stream->send_data_len > stream->send_offset
                                ? stream->send_data_len - (size_t)stream->send_offset
                                : 0u,
                            stream->flow.blocked_pending);
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
