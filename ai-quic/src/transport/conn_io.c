#include "transport_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_quic/log.h"
#include "ai_quic/fs.h"

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
  return AI_QUIC_OK;
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

  if (space->ack_needed && space->largest_acked != UINT64_MAX) {
    ai_quic_build_ack_frame(&packet->frames[frame_index++], space->largest_acked);
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
  if (space == NULL || !space->ack_needed || space->largest_acked == UINT64_MAX ||
      space->key_state != AI_QUIC_KEY_STATE_INSTALLED) {
    return AI_QUIC_ERROR;
  }

  type = space_id == AI_QUIC_PN_SPACE_INITIAL ? AI_QUIC_PACKET_TYPE_INITIAL
                                              : AI_QUIC_PACKET_TYPE_HANDSHAKE;
  ai_quic_packet_init(packet, type, conn, space_id);
  ai_quic_build_ack_frame(&packet->frames[0], space->largest_acked);
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

static ai_quic_result_t ai_quic_conn_send_request(ai_quic_conn_impl_t *conn,
                                                  ai_quic_pending_datagram_t *pending,
                                                  size_t *pending_count,
                                                  size_t pending_capacity) {
  ai_quic_packet_t packet;
  ai_quic_frame_t *frame;
  int written;

  ai_quic_packet_init(&packet, AI_QUIC_PACKET_TYPE_ONE_RTT, conn, AI_QUIC_PN_SPACE_APP_DATA);
  frame = &packet.frames[0];
  frame->type = AI_QUIC_FRAME_STREAM;
  frame->payload.stream.stream_id = AI_QUIC_HTTP09_STREAM_ID;
  frame->payload.stream.offset = conn->stream.send_offset;
  written = snprintf((char *)frame->payload.stream.data,
                     sizeof(frame->payload.stream.data),
                     "GET %s\r\n",
                     conn->requested_path[0] != '\0' ? conn->requested_path : "/");
  if (written < 0) {
    return AI_QUIC_ERROR;
  }
  frame->payload.stream.data_len = (size_t)written;
  frame->payload.stream.fin = 1;
  packet.frame_count = 1u;
  conn->stream.send_offset += frame->payload.stream.data_len;
  return ai_quic_conn_push_datagram(conn, &packet, 1u, pending, pending_count, pending_capacity);
}

static ai_quic_result_t ai_quic_conn_send_response(ai_quic_conn_impl_t *conn,
                                                   const char *www_root,
                                                   ai_quic_pending_datagram_t *pending,
                                                   size_t *pending_count,
                                                   size_t pending_capacity) {
  ai_quic_packet_t packet;
  ai_quic_frame_t *frame;
  char filename[256];
  char full_path[1024];
  uint8_t *file_data;
  size_t file_len;

  if (!ai_quic_path_extract_filename(conn->requested_path, filename, sizeof(filename))) {
    return AI_QUIC_ERROR;
  }

  snprintf(full_path, sizeof(full_path), "%s/%s", www_root, filename);
  if (ai_quic_fs_read_binary_file(full_path, &file_data, &file_len) != AI_QUIC_OK ||
      file_len > AI_QUIC_MAX_FRAME_PAYLOAD_LEN) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "unable to read %s", full_path);
    return AI_QUIC_ERROR;
  }

  ai_quic_packet_init(&packet, AI_QUIC_PACKET_TYPE_ONE_RTT, conn, AI_QUIC_PN_SPACE_APP_DATA);
  frame = &packet.frames[0];
  frame->type = AI_QUIC_FRAME_STREAM;
  frame->payload.stream.stream_id = AI_QUIC_HTTP09_STREAM_ID;
  frame->payload.stream.offset = conn->stream.send_offset;
  frame->payload.stream.data_len = file_len;
  frame->payload.stream.fin = 1;
  memcpy(frame->payload.stream.data, file_data, file_len);
  packet.frame_count = 1u;
  conn->stream.send_offset += file_len;
  free(file_data);
  conn->response_finished = 1;
  return ai_quic_conn_push_datagram(conn, &packet, 1u, pending, pending_count, pending_capacity);
}

static ai_quic_result_t ai_quic_conn_write_download(ai_quic_conn_impl_t *conn,
                                                    const char *downloads_root) {
  char filename[256];
  char full_path[1024];

  if (downloads_root == NULL ||
      !ai_quic_path_extract_filename(conn->requested_path, filename, sizeof(filename))) {
    return AI_QUIC_ERROR;
  }

  snprintf(full_path, sizeof(full_path), "%s/%s", downloads_root, filename);
  return ai_quic_fs_write_binary_file(full_path,
                                      conn->stream.recv_data,
                                      conn->stream.recv_data_len);
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

  if (conn == NULL || pending == NULL || pending_count == NULL) {
    return AI_QUIC_ERROR;
  }

  initial_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_INITIAL);
  handshake_space = ai_quic_conn_space(conn, AI_QUIC_PN_SPACE_HANDSHAKE);
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
                       initial_space->largest_acked != UINT64_MAX;

    if (have_initial_ack) {
      ai_quic_packet_init(&packets[packet_count],
                          AI_QUIC_PACKET_TYPE_INITIAL,
                          conn,
                          AI_QUIC_PN_SPACE_INITIAL);
      ai_quic_build_ack_frame(&packets[packet_count].frames[0],
                              initial_space->largest_acked);
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
                      (unsigned long long)initial_space->largest_acked,
                      (unsigned long long)handshake_space->largest_acked,
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

  if (!conn->is_server && conn->handshake_confirmed && conn->stream.send_fin == 0 &&
      conn->can_send_1rtt) {
    conn->stream.send_fin = 1;
    if (ai_quic_conn_send_request(conn, pending, pending_count, pending_capacity) !=
        AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
  }

  if (conn->is_server && conn->request_received && !conn->response_finished &&
      conn->can_send_1rtt) {
    if (ai_quic_conn_send_response(conn,
                                   www_root,
                                   pending,
                                   pending_count,
                                   pending_capacity) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
  }

  (void)now_ms;
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

  if (request_path != NULL) {
    ai_quic_set_error(conn->requested_path, sizeof(conn->requested_path), "%s", request_path);
  } else {
    ai_quic_set_error(conn->requested_path, sizeof(conn->requested_path), "%s", "/");
  }

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
  }
  if (conn->is_server && space_id == AI_QUIC_PN_SPACE_INITIAL &&
      !conn->peer_transport_params_validated) {
    if (ai_quic_transport_params_validate_server(&conn->peer_transport_params,
                                                 &packet->header.scid) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "server TP validation failed");
      return AI_QUIC_ERROR;
    }
    conn->peer_transport_params_validated = 1;
  }

  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_conn_on_stream(ai_quic_conn_impl_t *conn,
                                               const ai_quic_stream_frame_t *frame,
                                               const char *downloads_root) {
  char filename[256];

  if (conn == NULL || frame == NULL) {
    return AI_QUIC_ERROR;
  }

  if (!conn->can_send_1rtt) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "received STREAM before 1-RTT ready");
    return AI_QUIC_ERROR;
  }

  if (ai_quic_stream_on_receive(&conn->stream, frame) != AI_QUIC_OK) {
    ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "stream receive failed");
    return AI_QUIC_ERROR;
  }

  if (conn->is_server && conn->stream.recv_fin && !conn->request_received) {
    if (sscanf((const char *)conn->stream.recv_data, "GET %511s", conn->requested_path) != 1) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "invalid request");
      return AI_QUIC_ERROR;
    }
    conn->request_received = 1;
  } else if (!conn->is_server && conn->stream.recv_fin) {
    if (!ai_quic_path_extract_filename(conn->requested_path, filename, sizeof(filename))) {
      return AI_QUIC_ERROR;
    }
    if (ai_quic_conn_write_download(conn, downloads_root) != AI_QUIC_OK) {
      ai_quic_set_error(conn->last_error, sizeof(conn->last_error), "%s", "download write failed");
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

  for (i = 0; i < packet->frame_count; ++i) {
    const ai_quic_frame_t *frame = &packet->frames[i];
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
        if (ai_quic_conn_on_stream(conn, &frame->payload.stream, downloads_root) !=
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
      case AI_QUIC_FRAME_CONNECTION_CLOSE:
        conn->state = AI_QUIC_CONN_STATE_CLOSING;
        break;
      case AI_QUIC_FRAME_PADDING:
      default:
        break;
    }
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

  (void)www_root;
  (void)pending;
  (void)pending_count;
  (void)pending_capacity;
  return AI_QUIC_OK;
}
