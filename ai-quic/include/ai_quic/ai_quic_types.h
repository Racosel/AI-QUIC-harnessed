#ifndef AI_QUIC_TYPES_H
#define AI_QUIC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define AIQ_MAX_CID_LEN 20
#define AIQ_PN_SPACE_NUM 3
#define AIQ_PN_SENT_RECORD_CAP 64

typedef enum {
  AIQ_ROLE_CLIENT = 0,
  AIQ_ROLE_SERVER = 1,
} aiq_role_t;

typedef enum {
  AIQ_CONN_STATE_SERVER_INIT = 0,
  AIQ_CONN_STATE_SERVER_INITIAL_RECV = 1,
  AIQ_CONN_STATE_SERVER_INITIAL_SENT = 2,
  AIQ_CONN_STATE_SERVER_HANDSHAKE_SENT = 3,
  AIQ_CONN_STATE_SERVER_HANDSHAKE_RECV = 4,
  AIQ_CONN_STATE_CLIENT_INIT = 5,
  AIQ_CONN_STATE_CLIENT_INITIAL_SENT = 6,
  AIQ_CONN_STATE_CLIENT_HANDSHAKE_RECV = 7,
  AIQ_CONN_STATE_CLIENT_HANDSHAKE_SENT = 8,
  AIQ_CONN_STATE_ESTABLISHED = 9,
  AIQ_CONN_STATE_CLOSING = 10,
  AIQ_CONN_STATE_CLOSED = 11,
  AIQ_CONN_STATE_N = 12,
} aiq_conn_state_t;

typedef enum {
  AIQ_PN_SPACE_INITIAL = 0,
  AIQ_PN_SPACE_HANDSHAKE = 1,
  AIQ_PN_SPACE_APP = 2,
} aiq_pn_space_t;

typedef struct {
  uint8_t len;
  uint8_t bytes[AIQ_MAX_CID_LEN];
} aiq_cid_t;

typedef struct {
  uint64_t pktno;
  uint64_t sent_time_us;
  uint8_t ack_eliciting;
} aiq_pkt_sent_record_t;

typedef struct {
  uint64_t next_pktno;
  uint64_t largest_acked;
  uint8_t largest_acked_valid;
  uint8_t ack_needed;
  uint64_t pto_deadline_us;
  aiq_pkt_sent_record_t sent_records[AIQ_PN_SENT_RECORD_CAP];
  size_t sent_record_count;
} aiq_pn_space_state_t;

typedef struct {
  uint64_t max_data;
  uint64_t used_data;
} aiq_fc_window_t;

#endif /* AI_QUIC_TYPES_H */
