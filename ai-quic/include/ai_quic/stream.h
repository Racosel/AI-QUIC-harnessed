#ifndef AI_QUIC_STREAM_H
#define AI_QUIC_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/frame.h"

#define AI_QUIC_HTTP09_STREAM_ID 0u
#define AI_QUIC_MAX_STREAMS 64u
#define AI_QUIC_MAX_TRACKED_STREAM_RANGES 32u
#define AI_QUIC_MAX_STREAM_REQUEST_PATH_LEN 512u
#define AI_QUIC_MAX_STREAM_OUTPUT_PATH_LEN 1024u
#define AI_QUIC_INITIAL_MAX_DATA (128u * 1024u)
/*
 * Keep the per-stream receive window aligned with the connection window.
 * In interop transfer runs, peers may continue filling a single response
 * stream up to the connection-level credit while earlier gaps are pending;
 * using a smaller stream window causes us to abort locally before MAX_DATA
 * can provide backpressure.
 */
#define AI_QUIC_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL AI_QUIC_INITIAL_MAX_DATA
#define AI_QUIC_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE AI_QUIC_INITIAL_MAX_DATA
/*
 * Leave a small explicit in-flight reserve beyond the nominal stream window.
 * During transfer interop runs, peers can have a few STREAM chunks already in
 * flight when our latest MAX_STREAM_DATA update is still propagating.
 */
#define AI_QUIC_STREAM_RECV_LIMIT_RESERVE (4u * AI_QUIC_MAX_STREAM_SEND_CHUNK_LEN)
#define AI_QUIC_CONN_RECV_LIMIT_RESERVE AI_QUIC_STREAM_RECV_LIMIT_RESERVE
#define AI_QUIC_FLOW_LOG_PROGRESS_STEP (256u * 1024u)

typedef struct ai_quic_stream_range {
  uint64_t start;
  uint64_t end;
} ai_quic_stream_range_t;

typedef struct ai_quic_stream_range_set {
  size_t count;
  ai_quic_stream_range_t ranges[AI_QUIC_MAX_TRACKED_STREAM_RANGES];
} ai_quic_stream_range_set_t;

typedef struct ai_quic_flow_controller {
  uint64_t initial_window;
  uint64_t recv_limit;
  uint64_t recv_consumed;
  uint64_t highest_received;
  uint64_t send_limit;
  uint64_t last_blocked_limit;
  int update_pending;
  int blocked_pending;
} ai_quic_flow_controller_t;

typedef struct ai_quic_stream_state {
  uint64_t stream_id;
  int in_use;
  int is_local;
  uint64_t send_offset;
  uint64_t resend_offset;
  uint64_t resend_end_offset;
  uint64_t resend_wrap_offset;
  uint64_t recv_contiguous_end;
  uint64_t app_consumed;
  uint64_t file_written_offset;
  uint64_t final_size;
  size_t recv_capacity;
  uint8_t *recv_data;
  uint8_t *recv_map;
  uint8_t *send_data;
  size_t send_data_len;
  ai_quic_flow_controller_t flow;
  ai_quic_stream_range_set_t acked_ranges;
  ai_quic_stream_range_set_t lost_ranges;
  int recv_fin;
  int send_fin_requested;
  int send_fin_sent;
  int send_fin_acked;
  int lost_fin_pending;
  int resend_pending;
  int final_size_known;
  int request_parsed;
  int response_prepared;
  int response_finished;
  int download_finished;
  char last_error[256];
  char request_path[AI_QUIC_MAX_STREAM_REQUEST_PATH_LEN];
  char output_path[AI_QUIC_MAX_STREAM_OUTPUT_PATH_LEN];
} ai_quic_stream_state_t;

typedef struct ai_quic_stream_manager {
  ai_quic_stream_state_t streams[AI_QUIC_MAX_STREAMS];
  size_t stream_count;
  size_t round_robin_cursor;
  uint64_t next_local_bidi_stream_id;
} ai_quic_stream_manager_t;

#endif
