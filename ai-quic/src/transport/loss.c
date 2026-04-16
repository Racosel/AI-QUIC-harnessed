#include "transport_internal.h"

#include <limits.h>
#include <string.h>

#include "ai_quic/log.h"

#define AI_QUIC_INITIAL_RTT_MS 333u
#define AI_QUIC_INITIAL_RTTVAR_MS (AI_QUIC_INITIAL_RTT_MS / 2u)
#define AI_QUIC_MAX_ACK_DELAY_MS 25u
#define AI_QUIC_K_GRANULARITY_MS 1u
#define AI_QUIC_K_PACKET_THRESHOLD 3u
#define AI_QUIC_K_TIME_THRESHOLD_NUM 9u
#define AI_QUIC_K_TIME_THRESHOLD_DEN 8u

static uint64_t ai_quic_loss_max_u64(uint64_t a, uint64_t b) {
  return a > b ? a : b;
}

static uint64_t ai_quic_loss_initial_rtt(uint64_t value, uint64_t fallback) {
  return value != 0u ? value : fallback;
}

static int ai_quic_ack_frame_contains(const ai_quic_ack_frame_t *ack, uint64_t packet_number) {
  uint64_t range_end;
  uint64_t range_start;
  size_t i;

  if (ack == NULL || ack->largest_acked < ack->first_ack_range) {
    return 0;
  }

  range_end = ack->largest_acked;
  range_start = range_end - ack->first_ack_range;
  if (packet_number >= range_start && packet_number <= range_end) {
    return 1;
  }

  for (i = 0u; i < ack->ack_range_count; ++i) {
    if (range_start < ack->ack_ranges[i].gap + 2u) {
      return 0;
    }
    range_end = range_start - ack->ack_ranges[i].gap - 2u;
    if (range_end < ack->ack_ranges[i].ack_range) {
      return 0;
    }
    range_start = range_end - ack->ack_ranges[i].ack_range;
    if (packet_number >= range_start && packet_number <= range_end) {
      return 1;
    }
  }
  return 0;
}

static int ai_quic_loss_packet_is_ack_eliciting(const ai_quic_packet_t *packet) {
  size_t i;

  if (packet == NULL) {
    return 0;
  }

  for (i = 0u; i < packet->frame_count; ++i) {
    ai_quic_frame_type_t type = packet->frames[i].type;
    if (type != AI_QUIC_FRAME_ACK && type != AI_QUIC_FRAME_PADDING) {
      return 1;
    }
  }
  return 0;
}

static int ai_quic_loss_packet_is_in_flight(const ai_quic_packet_t *packet) {
  size_t i;

  if (packet == NULL) {
    return 0;
  }

  for (i = 0u; i < packet->frame_count; ++i) {
    if (packet->frames[i].type != AI_QUIC_FRAME_ACK) {
      return 1;
    }
  }
  return 0;
}

static void ai_quic_loss_remove_sent_packet(ai_quic_packet_number_space_t *space,
                                            ai_quic_loss_state_t *loss,
                                            ai_quic_sent_packet_t *sent) {
  if (space == NULL || loss == NULL || sent == NULL || !sent->in_use) {
    return;
  }

  if (sent->in_flight) {
    if (space->bytes_in_flight > sent->sent_bytes) {
      space->bytes_in_flight -= sent->sent_bytes;
    } else {
      space->bytes_in_flight = 0u;
    }
  }
  memset(sent, 0, sizeof(*sent));
  if (loss->sent_packet_count > 0u) {
    loss->sent_packet_count -= 1u;
  }
}

static uint64_t ai_quic_loss_compute_loss_delay(const ai_quic_conn_impl_t *conn) {
  uint64_t base_rtt;
  uint64_t delay;

  if (conn == NULL) {
    return AI_QUIC_INITIAL_RTT_MS;
  }

  base_rtt = ai_quic_loss_initial_rtt(conn->rtt_state.smoothed_rtt_ms, AI_QUIC_INITIAL_RTT_MS);
  base_rtt = ai_quic_loss_max_u64(base_rtt, conn->rtt_state.latest_rtt_ms);
  delay = (base_rtt * AI_QUIC_K_TIME_THRESHOLD_NUM + AI_QUIC_K_TIME_THRESHOLD_DEN - 1u) /
          AI_QUIC_K_TIME_THRESHOLD_DEN;
  return ai_quic_loss_max_u64(delay, AI_QUIC_K_GRANULARITY_MS);
}

static uint64_t ai_quic_loss_compute_pto(const ai_quic_conn_impl_t *conn,
                                         ai_quic_packet_number_space_id_t space_id,
                                         uint32_t pto_count) {
  uint64_t smoothed_rtt;
  uint64_t rttvar;
  uint64_t ack_delay;
  uint64_t duration;
  uint32_t backoff;

  if (conn == NULL) {
    return AI_QUIC_INITIAL_RTT_MS;
  }

  smoothed_rtt =
      ai_quic_loss_initial_rtt(conn->rtt_state.smoothed_rtt_ms, AI_QUIC_INITIAL_RTT_MS);
  rttvar = ai_quic_loss_initial_rtt(conn->rtt_state.rttvar_ms, AI_QUIC_INITIAL_RTTVAR_MS);
  ack_delay = space_id == AI_QUIC_PN_SPACE_APP_DATA ? conn->rtt_state.max_ack_delay_ms : 0u;
  duration =
      smoothed_rtt + ai_quic_loss_max_u64(4u * rttvar, AI_QUIC_K_GRANULARITY_MS) + ack_delay;
  duration = ai_quic_loss_max_u64(duration, AI_QUIC_K_GRANULARITY_MS);

  backoff = pto_count;
  while (backoff > 0u && duration <= UINT64_MAX / 2u) {
    duration *= 2u;
    backoff -= 1u;
  }
  if (backoff > 0u) {
    duration = UINT64_MAX;
  }
  return duration;
}

static void ai_quic_loss_update_rtt(ai_quic_conn_impl_t *conn, uint64_t sent_time_ms, uint64_t now_ms) {
  uint64_t latest_rtt;
  uint64_t adjusted_rtt;

  if (conn == NULL || now_ms < sent_time_ms) {
    return;
  }

  latest_rtt = now_ms - sent_time_ms;
  conn->rtt_state.latest_rtt_ms = latest_rtt;
  if (conn->rtt_state.min_rtt_ms == UINT64_MAX || latest_rtt < conn->rtt_state.min_rtt_ms) {
    conn->rtt_state.min_rtt_ms = latest_rtt;
  }

  adjusted_rtt = latest_rtt;
  if (conn->rtt_state.smoothed_rtt_ms == 0u) {
    conn->rtt_state.smoothed_rtt_ms = adjusted_rtt;
    conn->rtt_state.rttvar_ms = adjusted_rtt / 2u;
    return;
  }

  if (conn->rtt_state.smoothed_rtt_ms > adjusted_rtt) {
    conn->rtt_state.rttvar_ms =
        (3u * conn->rtt_state.rttvar_ms +
         (conn->rtt_state.smoothed_rtt_ms - adjusted_rtt)) /
        4u;
  } else {
    conn->rtt_state.rttvar_ms =
        (3u * conn->rtt_state.rttvar_ms +
         (adjusted_rtt - conn->rtt_state.smoothed_rtt_ms)) /
        4u;
  }
  conn->rtt_state.smoothed_rtt_ms =
      (7u * conn->rtt_state.smoothed_rtt_ms + adjusted_rtt) / 8u;
}

static ai_quic_result_t ai_quic_loss_track_app_packet(ai_quic_conn_impl_t *conn,
                                                      ai_quic_loss_state_t *loss,
                                                      const ai_quic_packet_t *packet,
                                                      size_t sent_bytes,
                                                      uint64_t now_ms) {
  size_t attempts;
  size_t slot_index;
  ai_quic_sent_packet_t *sent;
  size_t i;

  if (conn == NULL || loss == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  slot_index = loss->next_sent_packet_index;
  sent = NULL;
  for (attempts = 0u; attempts < AI_QUIC_ARRAY_LEN(loss->sent_packets); ++attempts) {
    size_t index = (slot_index + attempts) % AI_QUIC_ARRAY_LEN(loss->sent_packets);
    if (!loss->sent_packets[index].in_use) {
      sent = &loss->sent_packets[index];
      loss->next_sent_packet_index = (index + 1u) % AI_QUIC_ARRAY_LEN(loss->sent_packets);
      break;
    }
  }
  if (sent == NULL) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR,
                      "loss",
                      "sent packet table full space=app packet=%llu bytes=%zu",
                      (unsigned long long)packet->header.packet_number,
                      sent_bytes);
    return AI_QUIC_ERROR;
  }

  memset(sent, 0, sizeof(*sent));
  sent->in_use = 1;
  sent->space_id = AI_QUIC_PN_SPACE_APP_DATA;
  sent->packet_number = packet->header.packet_number;
  sent->time_sent_ms = now_ms;
  sent->sent_bytes = sent_bytes;
  sent->ack_eliciting = ai_quic_loss_packet_is_ack_eliciting(packet);
  sent->in_flight = ai_quic_loss_packet_is_in_flight(packet);
  for (i = 0u; i < packet->frame_count; ++i) {
    if (packet->frames[i].type != AI_QUIC_FRAME_STREAM) {
      continue;
    }
    if (sent->stream_frame_count >= AI_QUIC_ARRAY_LEN(sent->stream_frames)) {
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "loss",
                        "too many tracked stream frames packet=%llu count=%zu",
                        (unsigned long long)packet->header.packet_number,
                        sent->stream_frame_count + 1u);
      memset(sent, 0, sizeof(*sent));
      return AI_QUIC_ERROR;
    }
    sent->stream_frames[sent->stream_frame_count].stream_id =
        packet->frames[i].payload.stream.stream_id;
    sent->stream_frames[sent->stream_frame_count].offset =
        packet->frames[i].payload.stream.offset;
    sent->stream_frames[sent->stream_frame_count].end_offset =
        packet->frames[i].payload.stream.offset + packet->frames[i].payload.stream.data_len;
    sent->stream_frames[sent->stream_frame_count].fin = packet->frames[i].payload.stream.fin;
    sent->stream_frame_count += 1u;
  }
  loss->sent_packet_count += 1u;
  ai_quic_log_write(AI_QUIC_LOG_DEBUG,
                    "loss",
                    "packet_sent space=app pn=%llu bytes=%zu ack_eliciting=%d in_flight=%d stream_frames=%zu outstanding=%zu",
                    (unsigned long long)sent->packet_number,
                    sent->sent_bytes,
                    sent->ack_eliciting,
                    sent->in_flight,
                    sent->stream_frame_count,
                    loss->sent_packet_count);
  return AI_QUIC_OK;
}

static void ai_quic_loss_detect_lost_packets(ai_quic_conn_impl_t *conn,
                                             ai_quic_packet_number_space_id_t space_id,
                                             uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;
  ai_quic_loss_state_t *loss;
  uint64_t loss_delay;
  size_t i;

  if (conn == NULL) {
    return;
  }
  if (space_id != AI_QUIC_PN_SPACE_APP_DATA) {
    return;
  }

  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }
  loss = &conn->loss_state[space_id];
  if (loss->largest_acked_packet == UINT64_MAX) {
    return;
  }

  loss_delay = ai_quic_loss_compute_loss_delay(conn);
  for (i = 0u; i < AI_QUIC_ARRAY_LEN(loss->sent_packets); ++i) {
    ai_quic_sent_packet_t *sent = &loss->sent_packets[i];
    int packet_lost;
    size_t frame_index;

    if (!sent->in_use || !sent->in_flight) {
      continue;
    }

    packet_lost = 0;
    if (loss->largest_acked_packet >= sent->packet_number &&
        loss->largest_acked_packet - sent->packet_number >= AI_QUIC_K_PACKET_THRESHOLD) {
      packet_lost = 1;
    } else if (now_ms >= sent->time_sent_ms &&
               now_ms - sent->time_sent_ms >= loss_delay) {
      packet_lost = 1;
    }
    if (!packet_lost) {
      continue;
    }

    ai_quic_log_write(AI_QUIC_LOG_DEBUG,
                      "loss",
                      "packet_lost space=app pn=%llu bytes=%zu largest_acked=%llu loss_delay=%llu",
                      (unsigned long long)sent->packet_number,
                      sent->sent_bytes,
                      (unsigned long long)loss->largest_acked_packet,
                      (unsigned long long)loss_delay);
    for (frame_index = 0u; frame_index < sent->stream_frame_count; ++frame_index) {
      ai_quic_stream_state_t *stream =
          ai_quic_stream_manager_find(&conn->streams, sent->stream_frames[frame_index].stream_id);
      if (stream == NULL) {
        continue;
      }
      if (ai_quic_stream_mark_lost(stream,
                                   sent->stream_frames[frame_index].offset,
                                   sent->stream_frames[frame_index].end_offset,
                                   sent->stream_frames[frame_index].fin) != AI_QUIC_OK) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "loss",
                          "failed to mark lost stream=%llu start=%llu end=%llu fin=%d",
                          (unsigned long long)sent->stream_frames[frame_index].stream_id,
                          (unsigned long long)sent->stream_frames[frame_index].offset,
                          (unsigned long long)sent->stream_frames[frame_index].end_offset,
                          sent->stream_frames[frame_index].fin);
      }
    }
    ai_quic_loss_remove_sent_packet(space, loss, sent);
  }
}

void ai_quic_rtt_state_init(ai_quic_rtt_state_t *rtt) {
  if (rtt == NULL) {
    return;
  }

  memset(rtt, 0, sizeof(*rtt));
  rtt->min_rtt_ms = UINT64_MAX;
  rtt->max_ack_delay_ms = AI_QUIC_MAX_ACK_DELAY_MS;
}

void ai_quic_loss_state_init(ai_quic_loss_state_t *loss) {
  if (loss == NULL) {
    return;
  }

  memset(loss, 0, sizeof(*loss));
  loss->largest_acked_packet = UINT64_MAX;
}

ai_quic_result_t ai_quic_loss_on_packet_sent(ai_quic_conn_impl_t *conn,
                                             ai_quic_packet_number_space_id_t space_id,
                                             const ai_quic_packet_t *packet,
                                             size_t sent_bytes,
                                             uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;
  ai_quic_loss_state_t *loss;
  int in_flight;

  if (conn == NULL || packet == NULL) {
    return AI_QUIC_ERROR;
  }

  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return AI_QUIC_ERROR;
  }

  loss = &conn->loss_state[space_id];
  loss->latest_sent_packet = packet->header.packet_number;
  in_flight = ai_quic_loss_packet_is_in_flight(packet);
  if (in_flight) {
    space->bytes_in_flight += sent_bytes;
  }

  if (space_id == AI_QUIC_PN_SPACE_APP_DATA && in_flight &&
      ai_quic_loss_track_app_packet(conn, loss, packet, sent_bytes, now_ms) != AI_QUIC_OK) {
    if (space->bytes_in_flight > sent_bytes) {
      space->bytes_in_flight -= sent_bytes;
    } else {
      space->bytes_in_flight = 0u;
    }
    return AI_QUIC_ERROR;
  }

  ai_quic_loss_update_timer(conn, space_id, now_ms);
  return AI_QUIC_OK;
}

void ai_quic_loss_on_ack_received(ai_quic_conn_impl_t *conn,
                                  ai_quic_packet_number_space_id_t space_id,
                                  const ai_quic_ack_frame_t *ack,
                                  uint64_t now_ms,
                                  int *ack_progress_out) {
  ai_quic_packet_number_space_t *space;
  ai_quic_loss_state_t *loss;
  uint64_t prev_largest_acked;
  int ack_progress;
  uint64_t latest_acked_packet_number;
  uint64_t latest_acked_sent_time;
  size_t i;

  if (ack_progress_out != NULL) {
    *ack_progress_out = 0;
  }
  if (conn == NULL || ack == NULL) {
    return;
  }

  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }
  loss = &conn->loss_state[space_id];
  prev_largest_acked = space->largest_acked_by_peer;
  ack_progress = prev_largest_acked == UINT64_MAX || ack->largest_acked > prev_largest_acked;
  ai_quic_pn_space_on_ack(space, ack->largest_acked);
  if (loss->largest_acked_packet == UINT64_MAX || ack->largest_acked > loss->largest_acked_packet) {
    loss->largest_acked_packet = ack->largest_acked;
  }

  latest_acked_packet_number = UINT64_MAX;
  latest_acked_sent_time = 0u;
  if (space_id == AI_QUIC_PN_SPACE_APP_DATA) {
    for (i = 0u; i < AI_QUIC_ARRAY_LEN(loss->sent_packets); ++i) {
      ai_quic_sent_packet_t *sent = &loss->sent_packets[i];
      size_t frame_index;

      if (!sent->in_use || sent->space_id != space_id ||
          !ai_quic_ack_frame_contains(ack, sent->packet_number)) {
        continue;
      }

      if (sent->ack_eliciting &&
          (latest_acked_packet_number == UINT64_MAX ||
           sent->packet_number > latest_acked_packet_number)) {
        latest_acked_packet_number = sent->packet_number;
        latest_acked_sent_time = sent->time_sent_ms;
      }
      ai_quic_log_write(AI_QUIC_LOG_DEBUG,
                        "loss",
                        "packet_acked space=app pn=%llu bytes=%zu stream_frames=%zu",
                        (unsigned long long)sent->packet_number,
                        sent->sent_bytes,
                        sent->stream_frame_count);
      for (frame_index = 0u; frame_index < sent->stream_frame_count; ++frame_index) {
        ai_quic_stream_state_t *stream =
            ai_quic_stream_manager_find(&conn->streams, sent->stream_frames[frame_index].stream_id);
        if (stream == NULL) {
          continue;
        }
        if (ai_quic_stream_mark_acked(stream,
                                      sent->stream_frames[frame_index].offset,
                                      sent->stream_frames[frame_index].end_offset,
                                      sent->stream_frames[frame_index].fin) != AI_QUIC_OK) {
          ai_quic_log_write(AI_QUIC_LOG_ERROR,
                            "loss",
                            "failed to mark acked stream=%llu start=%llu end=%llu fin=%d",
                            (unsigned long long)sent->stream_frames[frame_index].stream_id,
                            (unsigned long long)sent->stream_frames[frame_index].offset,
                            (unsigned long long)sent->stream_frames[frame_index].end_offset,
                            sent->stream_frames[frame_index].fin);
        }
      }
      ai_quic_loss_remove_sent_packet(space, loss, sent);
    }
  } else if (ack_progress) {
    space->bytes_in_flight = 0u;
  }

  if (latest_acked_packet_number != UINT64_MAX) {
    ai_quic_loss_update_rtt(conn, latest_acked_sent_time, now_ms);
    loss->last_ack_ms = now_ms;
    loss->pto_count = 0u;
  }
  if (ack_progress_out != NULL) {
    *ack_progress_out = ack_progress;
  }

  ai_quic_loss_detect_lost_packets(conn, space_id, now_ms);
  ai_quic_loss_update_timer(conn, space_id, now_ms);
}

void ai_quic_loss_update_timer(ai_quic_conn_impl_t *conn,
                               ai_quic_packet_number_space_id_t space_id,
                               uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;
  ai_quic_loss_state_t *loss;

  if (conn == NULL) {
    return;
  }
  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }
  loss = &conn->loss_state[space_id];

  if (space_id != AI_QUIC_PN_SPACE_APP_DATA) {
    if (space->bytes_in_flight > 0u &&
        (space_id != AI_QUIC_PN_SPACE_APP_DATA || conn->handshake_confirmed)) {
      space->pto_deadline_ms = now_ms + 100u;
      loss->timer_mode = AI_QUIC_LOSS_TIMER_MODE_PTO;
    } else {
      space->pto_deadline_ms = 0u;
      loss->timer_mode = AI_QUIC_LOSS_TIMER_MODE_NONE;
    }
    loss->loss_time_ms = 0u;
    return;
  }

  if (!conn->handshake_confirmed || loss->sent_packet_count == 0u || space->bytes_in_flight == 0u) {
    space->pto_deadline_ms = 0u;
    loss->loss_time_ms = 0u;
    loss->timer_mode = AI_QUIC_LOSS_TIMER_MODE_NONE;
    return;
  }

  {
    uint64_t loss_time;
    uint64_t pto_time;
    uint64_t loss_delay;
    uint64_t last_ack_eliciting_sent_ms;
    size_t i;

    loss_time = 0u;
    pto_time = 0u;
    loss_delay = ai_quic_loss_compute_loss_delay(conn);
    last_ack_eliciting_sent_ms = 0u;
    for (i = 0u; i < AI_QUIC_ARRAY_LEN(loss->sent_packets); ++i) {
      const ai_quic_sent_packet_t *sent = &loss->sent_packets[i];
      uint64_t candidate_time;

      if (!sent->in_use || !sent->in_flight) {
        continue;
      }

      candidate_time = sent->time_sent_ms + loss_delay;
      if (loss->largest_acked_packet != UINT64_MAX &&
          (loss_time == 0u || candidate_time < loss_time)) {
        loss_time = candidate_time;
      }
      if (sent->ack_eliciting && sent->time_sent_ms > last_ack_eliciting_sent_ms) {
        last_ack_eliciting_sent_ms = sent->time_sent_ms;
      }
    }
    if (last_ack_eliciting_sent_ms != 0u) {
      pto_time = last_ack_eliciting_sent_ms +
                 ai_quic_loss_compute_pto(conn, space_id, loss->pto_count);
    }

    loss->loss_time_ms = loss_time;
    if (loss_time != 0u && (pto_time == 0u || loss_time <= pto_time)) {
      space->pto_deadline_ms = loss_time;
      loss->timer_mode = AI_QUIC_LOSS_TIMER_MODE_LOSS;
    } else if (pto_time != 0u) {
      space->pto_deadline_ms = pto_time;
      loss->timer_mode = AI_QUIC_LOSS_TIMER_MODE_PTO;
    } else {
      space->pto_deadline_ms = 0u;
      loss->timer_mode = AI_QUIC_LOSS_TIMER_MODE_NONE;
    }
  }
}

int ai_quic_loss_on_timeout(ai_quic_conn_impl_t *conn,
                            ai_quic_packet_number_space_id_t space_id,
                            uint64_t now_ms) {
  ai_quic_packet_number_space_t *space;
  ai_quic_loss_state_t *loss;

  if (conn == NULL) {
    return 0;
  }
  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL || space->pto_deadline_ms == 0u || now_ms < space->pto_deadline_ms) {
    return 0;
  }
  loss = &conn->loss_state[space_id];

  if (space_id == AI_QUIC_PN_SPACE_APP_DATA &&
      loss->timer_mode == AI_QUIC_LOSS_TIMER_MODE_LOSS) {
    ai_quic_loss_detect_lost_packets(conn, space_id, now_ms);
    ai_quic_loss_update_timer(conn, space_id, now_ms);
    return 0;
  }

  if (loss->pto_count != UINT32_MAX) {
    loss->pto_count += 1u;
  }
  loss->last_pto_ms = now_ms;
  ai_quic_loss_update_timer(conn, space_id, now_ms);
  return 1;
}

void ai_quic_loss_discard_space(ai_quic_conn_impl_t *conn,
                                ai_quic_packet_number_space_id_t space_id) {
  ai_quic_packet_number_space_t *space;
  ai_quic_loss_state_t *loss;

  if (conn == NULL) {
    return;
  }
  space = ai_quic_conn_space(conn, space_id);
  if (space == NULL) {
    return;
  }
  ai_quic_pn_space_mark_key_discarded(space);
  loss = &conn->loss_state[space_id];
  ai_quic_loss_state_init(loss);
}
