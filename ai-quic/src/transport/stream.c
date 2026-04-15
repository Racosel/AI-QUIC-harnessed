#include "transport_internal.h"

#include <stdlib.h>
#include <string.h>

static void ai_quic_stream_state_reset(ai_quic_stream_state_t *stream,
                                       uint64_t stream_id,
                                       int is_local,
                                       uint64_t send_limit,
                                       uint64_t recv_limit) {
  if (stream == NULL) {
    return;
  }

  free(stream->recv_data);
  free(stream->recv_map);
  free(stream->send_data);
  memset(stream, 0, sizeof(*stream));
  stream->in_use = 1;
  stream->stream_id = stream_id;
  stream->is_local = is_local;
  ai_quic_flow_controller_init(&stream->flow, recv_limit);
  ai_quic_flow_controller_set_send_limit(&stream->flow, send_limit);
}

static ai_quic_result_t ai_quic_stream_reserve_recv(ai_quic_stream_state_t *stream,
                                                    uint64_t target_len) {
  uint8_t *new_data;
  uint8_t *new_map;
  size_t new_capacity;

  if (stream == NULL) {
    return AI_QUIC_ERROR;
  }
  if (target_len <= stream->recv_capacity) {
    return AI_QUIC_OK;
  }
  if (target_len > (uint64_t)SIZE_MAX) {
    return AI_QUIC_ERROR;
  }

  new_capacity = stream->recv_capacity == 0u ? 4096u : stream->recv_capacity;
  while ((uint64_t)new_capacity < target_len) {
    if (new_capacity > SIZE_MAX / 2u) {
      new_capacity = (size_t)target_len;
      break;
    }
    new_capacity *= 2u;
  }

  new_data = (uint8_t *)realloc(stream->recv_data, new_capacity);
  new_map = (uint8_t *)realloc(stream->recv_map, new_capacity);
  if (new_data == NULL || new_map == NULL) {
    if (new_data != NULL) {
      stream->recv_data = new_data;
    }
    if (new_map != NULL) {
      stream->recv_map = new_map;
    }
    return AI_QUIC_ERROR;
  }

  if (new_capacity > stream->recv_capacity) {
    memset(new_data + stream->recv_capacity, 0, new_capacity - stream->recv_capacity);
    memset(new_map + stream->recv_capacity, 0, new_capacity - stream->recv_capacity);
  }
  stream->recv_data = new_data;
  stream->recv_map = new_map;
  stream->recv_capacity = new_capacity;
  return AI_QUIC_OK;
}

void ai_quic_flow_controller_init(ai_quic_flow_controller_t *controller,
                                  uint64_t initial_window) {
  if (controller == NULL) {
    return;
  }

  memset(controller, 0, sizeof(*controller));
  controller->initial_window = initial_window;
  controller->recv_limit = initial_window;
  controller->send_limit = initial_window;
}

void ai_quic_flow_controller_set_send_limit(ai_quic_flow_controller_t *controller,
                                            uint64_t send_limit) {
  if (controller == NULL) {
    return;
  }
  if (send_limit > controller->send_limit) {
    controller->send_limit = send_limit;
    controller->blocked_pending = 0;
  }
}

void ai_quic_stream_manager_init(ai_quic_stream_manager_t *manager) {
  if (manager == NULL) {
    return;
  }

  memset(manager, 0, sizeof(*manager));
  manager->next_local_bidi_stream_id = AI_QUIC_HTTP09_STREAM_ID;
}

void ai_quic_stream_manager_cleanup(ai_quic_stream_manager_t *manager) {
  size_t i;

  if (manager == NULL) {
    return;
  }

  for (i = 0; i < AI_QUIC_ARRAY_LEN(manager->streams); ++i) {
    free(manager->streams[i].recv_data);
    free(manager->streams[i].recv_map);
    free(manager->streams[i].send_data);
    memset(&manager->streams[i], 0, sizeof(manager->streams[i]));
  }
  memset(manager, 0, sizeof(*manager));
}

ai_quic_stream_state_t *ai_quic_stream_manager_find(ai_quic_stream_manager_t *manager,
                                                    uint64_t stream_id) {
  size_t i;

  if (manager == NULL) {
    return NULL;
  }

  for (i = 0; i < AI_QUIC_ARRAY_LEN(manager->streams); ++i) {
    if (manager->streams[i].in_use && manager->streams[i].stream_id == stream_id) {
      return &manager->streams[i];
    }
  }
  return NULL;
}

static ai_quic_stream_state_t *ai_quic_stream_manager_alloc(ai_quic_stream_manager_t *manager) {
  size_t i;

  if (manager == NULL) {
    return NULL;
  }

  for (i = 0; i < AI_QUIC_ARRAY_LEN(manager->streams); ++i) {
    if (!manager->streams[i].in_use) {
      manager->stream_count += 1u;
      return &manager->streams[i];
    }
  }
  return NULL;
}

ai_quic_stream_state_t *ai_quic_stream_manager_open_local_bidi(
    ai_quic_stream_manager_t *manager,
    uint64_t send_limit,
    uint64_t recv_limit) {
  ai_quic_stream_state_t *stream;
  uint64_t stream_id;

  if (manager == NULL) {
    return NULL;
  }

  stream_id = manager->next_local_bidi_stream_id;
  manager->next_local_bidi_stream_id += 4u;
  stream = ai_quic_stream_manager_alloc(manager);
  if (stream == NULL) {
    return NULL;
  }
  ai_quic_stream_state_reset(stream, stream_id, 1, send_limit, recv_limit);
  return stream;
}

ai_quic_stream_state_t *ai_quic_stream_manager_get_or_create_remote_bidi(
    ai_quic_stream_manager_t *manager,
    uint64_t stream_id,
    uint64_t send_limit,
    uint64_t recv_limit) {
  ai_quic_stream_state_t *stream;

  if (manager == NULL || (stream_id & 0x03u) != 0u) {
    return NULL;
  }

  stream = ai_quic_stream_manager_find(manager, stream_id);
  if (stream != NULL) {
    ai_quic_flow_controller_set_send_limit(&stream->flow, send_limit);
    if (recv_limit > stream->flow.recv_limit) {
      stream->flow.recv_limit = recv_limit;
    }
    if (recv_limit > stream->flow.initial_window) {
      stream->flow.initial_window = recv_limit;
    }
    return stream;
  }

  stream = ai_quic_stream_manager_alloc(manager);
  if (stream == NULL) {
    return NULL;
  }
  ai_quic_stream_state_reset(stream, stream_id, 0, send_limit, recv_limit);
  return stream;
}

ai_quic_result_t ai_quic_stream_prepare_send(ai_quic_stream_state_t *stream,
                                             const uint8_t *data,
                                             size_t data_len,
                                             int send_fin,
                                             const char *request_path,
                                             const char *output_path) {
  uint8_t *copy;

  if (stream == NULL || (data_len > 0u && data == NULL)) {
    return AI_QUIC_ERROR;
  }

  copy = NULL;
  if (data_len > 0u) {
    copy = (uint8_t *)malloc(data_len);
    if (copy == NULL) {
      return AI_QUIC_ERROR;
    }
    memcpy(copy, data, data_len);
  }

  free(stream->send_data);
  stream->send_data = copy;
  stream->send_data_len = data_len;
  stream->send_offset = 0u;
  stream->resend_offset = 0u;
  stream->resend_end_offset = 0u;
  stream->resend_wrap_offset = 0u;
  stream->send_fin_requested = send_fin;
  stream->send_fin_sent = 0;
  stream->resend_pending = 0;
  stream->response_finished = 0;
  if (request_path != NULL) {
    ai_quic_set_error(stream->request_path, sizeof(stream->request_path), "%s", request_path);
  }
  if (output_path != NULL) {
    ai_quic_set_error(stream->output_path, sizeof(stream->output_path), "%s", output_path);
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_stream_on_receive(ai_quic_stream_state_t *stream,
                                           const ai_quic_stream_frame_t *frame) {
  uint64_t frame_end;
  uint64_t cursor;

  if (stream == NULL || frame == NULL || frame->stream_id != stream->stream_id) {
    if (stream != NULL) {
      ai_quic_set_error(stream->last_error,
                        sizeof(stream->last_error),
                        "invalid stream receive args stream=%llu frame_stream=%llu",
                        (unsigned long long)stream->stream_id,
                        (unsigned long long)(frame == NULL ? UINT64_MAX : frame->stream_id));
    }
    return AI_QUIC_ERROR;
  }
  stream->last_error[0] = '\0';

  frame_end = frame->offset + frame->data_len;
  if (frame_end < frame->offset) {
    ai_quic_set_error(stream->last_error,
                      sizeof(stream->last_error),
                      "stream=%llu offset overflow offset=%llu len=%zu",
                      (unsigned long long)stream->stream_id,
                      (unsigned long long)frame->offset,
                      frame->data_len);
    return AI_QUIC_ERROR;
  }

  if (frame->fin) {
    if (stream->final_size_known && stream->final_size != frame_end) {
      ai_quic_set_error(stream->last_error,
                        sizeof(stream->last_error),
                        "stream=%llu final_size mismatch existing=%llu new=%llu offset=%llu len=%zu",
                        (unsigned long long)stream->stream_id,
                        (unsigned long long)stream->final_size,
                        (unsigned long long)frame_end,
                        (unsigned long long)frame->offset,
                        frame->data_len);
      return AI_QUIC_ERROR;
    }
    stream->final_size = frame_end;
    stream->final_size_known = 1;
    stream->recv_fin = 1;
  }
  if (stream->final_size_known && frame_end > stream->final_size) {
    ai_quic_set_error(stream->last_error,
                      sizeof(stream->last_error),
                      "stream=%llu frame exceeds final_size frame_end=%llu final_size=%llu offset=%llu len=%zu",
                      (unsigned long long)stream->stream_id,
                      (unsigned long long)frame_end,
                      (unsigned long long)stream->final_size,
                      (unsigned long long)frame->offset,
                      frame->data_len);
    return AI_QUIC_ERROR;
  }
  if (frame_end > stream->flow.recv_limit) {
    ai_quic_set_error(stream->last_error,
                      sizeof(stream->last_error),
                      "stream=%llu recv_limit exceeded frame_end=%llu recv_limit=%llu offset=%llu len=%zu app_consumed=%llu contiguous=%llu",
                      (unsigned long long)stream->stream_id,
                      (unsigned long long)frame_end,
                      (unsigned long long)stream->flow.recv_limit,
                      (unsigned long long)frame->offset,
                      frame->data_len,
                      (unsigned long long)stream->app_consumed,
                      (unsigned long long)stream->recv_contiguous_end);
    return AI_QUIC_ERROR;
  }

  if (frame_end > stream->flow.highest_received) {
    stream->flow.highest_received = frame_end;
  }

  if (frame->data_len > 0u) {
    if (ai_quic_stream_reserve_recv(stream, frame_end) != AI_QUIC_OK) {
      ai_quic_set_error(stream->last_error,
                        sizeof(stream->last_error),
                        "stream=%llu recv buffer reserve failed target=%llu",
                        (unsigned long long)stream->stream_id,
                        (unsigned long long)frame_end);
      return AI_QUIC_ERROR;
    }
    memcpy(stream->recv_data + (size_t)frame->offset, frame->data, frame->data_len);
    memset(stream->recv_map + (size_t)frame->offset, 1, frame->data_len);
  }

  cursor = stream->recv_contiguous_end;
  while (cursor < stream->recv_capacity && stream->recv_map[cursor] != 0u) {
    cursor += 1u;
  }
  stream->recv_contiguous_end = cursor;
  if (stream->final_size_known && stream->recv_contiguous_end > stream->final_size) {
    ai_quic_set_error(stream->last_error,
                      sizeof(stream->last_error),
                      "stream=%llu contiguous beyond final_size contiguous=%llu final_size=%llu",
                      (unsigned long long)stream->stream_id,
                      (unsigned long long)stream->recv_contiguous_end,
                      (unsigned long long)stream->final_size);
    return AI_QUIC_ERROR;
  }
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_stream_consume(ai_quic_stream_state_t *stream,
                                        uint64_t bytes) {
  uint64_t update_margin;
  uint64_t next_limit;

  if (stream == NULL || stream->app_consumed + bytes > stream->recv_contiguous_end) {
    return AI_QUIC_ERROR;
  }

  stream->app_consumed += bytes;
  stream->flow.recv_consumed = stream->app_consumed;
  update_margin = stream->flow.initial_window / 2u;
  update_margin += AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN;
  if (stream->flow.initial_window > 0u &&
      stream->flow.recv_consumed + update_margin >= stream->flow.recv_limit) {
    next_limit = stream->flow.recv_consumed + stream->flow.initial_window;
    next_limit += AI_QUIC_STREAM_RECV_LIMIT_RESERVE;
    stream->flow.recv_limit = next_limit;
    stream->flow.update_pending = 1;
  }
  return AI_QUIC_OK;
}
