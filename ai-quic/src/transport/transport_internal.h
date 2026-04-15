#ifndef AI_QUIC_TRANSPORT_INTERNAL_H
#define AI_QUIC_TRANSPORT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "common_internal.h"
#include "ai_quic/conn.h"
#include "ai_quic/dispatcher.h"
#include "ai_quic/endpoint.h"
#include "ai_quic/frame.h"
#include "ai_quic/packet.h"
#include "ai_quic/qlog.h"
#include "ai_quic/stream.h"
#include "ai_quic/tls.h"
#include "ai_quic/transport_params.h"

#define AI_QUIC_MAX_PENDING_DATAGRAMS 4096u
#define AI_QUIC_MAX_DATAGRAM_SIZE AI_QUIC_MAX_PACKET_SIZE
#define AI_QUIC_MAX_HTTP_BUFFER_LEN 8192u

typedef struct ai_quic_pending_datagram {
  size_t len;
  uint8_t bytes[AI_QUIC_MAX_DATAGRAM_SIZE];
} ai_quic_pending_datagram_t;

typedef struct ai_quic_loss_state {
  uint64_t latest_sent_packet;
} ai_quic_loss_state_t;

typedef struct ai_quic_conn {
  int is_server;
  ai_quic_version_t version;
  ai_quic_conn_state_t state;
  ai_quic_cid_t local_cid;
  ai_quic_cid_t peer_cid;
  ai_quic_cid_t original_destination_cid;
  ai_quic_transport_params_t local_transport_params;
  ai_quic_transport_params_t peer_transport_params;
  ai_quic_packet_number_space_t packet_spaces[AI_QUIC_PN_SPACE_COUNT];
  ai_quic_stream_manager_t streams;
  ai_quic_flow_controller_t conn_flow;
  ai_quic_tls_session_t *tls_session;
  ai_quic_qlog_writer_t *qlog;
  ai_quic_loss_state_t loss_state[AI_QUIC_PN_SPACE_COUNT];
  uint64_t bytes_received;
  uint64_t bytes_sent;
  uint64_t last_activity_ms;
  uint64_t last_peer_max_data_ms;
  uint64_t last_peer_stream_max_data_ms;
  uint64_t last_peer_data_blocked_ms;
  uint64_t app_flush_count;
  int handshake_completed;
  int can_send_1rtt;
  int handshake_confirmed;
  int address_validated;
  int peer_transport_params_validated;
  int should_send_handshake_done;
  int saw_first_server_initial;
  size_t total_request_streams;
  size_t completed_request_streams;
  char last_error[256];
} ai_quic_conn_impl_t;

typedef struct ai_quic_endpoint {
  ai_quic_endpoint_config_t config;
  ai_quic_dispatcher_t *dispatcher;
  ai_quic_tls_ctx_t *tls_ctx;
  ai_quic_qlog_writer_t *qlog;
  ai_quic_conn_impl_t *conn;
  ai_quic_pending_datagram_t pending[AI_QUIC_MAX_PENDING_DATAGRAMS];
  size_t pending_head;
  size_t pending_len;
  ai_quic_result_t status;
  char error[256];
  char authority[256];
  char request_path[512];
} ai_quic_endpoint_impl_t;

void ai_quic_set_error(char *buffer, size_t capacity, const char *format, ...);
ai_quic_packet_number_space_t *ai_quic_conn_space(ai_quic_conn_impl_t *conn,
                                                  ai_quic_packet_number_space_id_t id);
const ai_quic_packet_number_space_t *ai_quic_conn_space_const(
    const ai_quic_conn_impl_t *conn,
    ai_quic_packet_number_space_id_t id);
ai_quic_encryption_level_t ai_quic_space_to_level(
    ai_quic_packet_number_space_id_t id);
ai_quic_packet_number_space_id_t ai_quic_level_to_space(
    ai_quic_encryption_level_t level);

ai_quic_result_t ai_quic_packet_encode(const ai_quic_packet_t *packet,
                                       uint8_t *buffer,
                                       size_t capacity,
                                       size_t *written);
ai_quic_result_t ai_quic_packet_encode_conn(ai_quic_conn_impl_t *conn,
                                            const ai_quic_packet_t *packet,
                                            uint8_t *buffer,
                                            size_t capacity,
                                            size_t *written);
ai_quic_result_t ai_quic_packet_decode(const uint8_t *buffer,
                                       size_t buffer_len,
                                       size_t *consumed,
                                       ai_quic_packet_t *packet);
ai_quic_result_t ai_quic_packet_decode_conn(ai_quic_conn_impl_t *conn,
                                            const uint8_t *buffer,
                                            size_t buffer_len,
                                            size_t *consumed,
                                            ai_quic_packet_t *packet);
const char *ai_quic_packet_decode_last_error(void);
const char *ai_quic_packet_encode_last_error(void);
ai_quic_result_t ai_quic_frame_encode(const ai_quic_frame_t *frame,
                                      uint8_t *buffer,
                                      size_t capacity,
                                      size_t *written);
ai_quic_result_t ai_quic_frame_decode(const uint8_t *buffer,
                                      size_t buffer_len,
                                      size_t *consumed,
                                      ai_quic_frame_t *frame);

ai_quic_result_t ai_quic_pn_space_init(ai_quic_packet_number_space_t *space,
                                       ai_quic_packet_number_space_id_t id);
void ai_quic_pn_space_mark_key_installed(ai_quic_packet_number_space_t *space);
void ai_quic_pn_space_mark_key_discarded(ai_quic_packet_number_space_t *space);
void ai_quic_pn_space_on_packet_received(ai_quic_packet_number_space_t *space,
                                         uint64_t packet_number);
void ai_quic_pn_space_on_ack(ai_quic_packet_number_space_t *space,
                             uint64_t largest_acked);

void ai_quic_loss_on_packet_sent(ai_quic_conn_impl_t *conn,
                                 ai_quic_packet_number_space_id_t space_id,
                                 uint64_t packet_number,
                                 uint64_t now_ms);
void ai_quic_loss_discard_space(ai_quic_conn_impl_t *conn,
                                ai_quic_packet_number_space_id_t space_id);
void ai_quic_timer_on_space_active(ai_quic_conn_impl_t *conn,
                                   ai_quic_packet_number_space_id_t space_id,
                                   uint64_t now_ms);

ai_quic_result_t ai_quic_transport_params_encode(
    const ai_quic_transport_params_t *params,
    uint8_t *buffer,
    size_t capacity,
    size_t *written);
ai_quic_result_t ai_quic_transport_params_decode(
    const uint8_t *buffer,
    size_t buffer_len,
    ai_quic_transport_params_t *params);
ai_quic_result_t ai_quic_transport_params_validate_client(
    const ai_quic_transport_params_t *params,
    const ai_quic_cid_t *original_destination_cid,
    const ai_quic_cid_t *peer_scid);
ai_quic_result_t ai_quic_transport_params_validate_server(
    const ai_quic_transport_params_t *params,
    const ai_quic_cid_t *peer_scid);

void ai_quic_build_ack_frame(const ai_quic_packet_number_space_t *space,
                             ai_quic_frame_t *frame);
void ai_quic_flow_controller_init(ai_quic_flow_controller_t *controller,
                                  uint64_t initial_window);
void ai_quic_flow_controller_set_send_limit(ai_quic_flow_controller_t *controller,
                                            uint64_t send_limit);
void ai_quic_stream_manager_init(ai_quic_stream_manager_t *manager);
void ai_quic_stream_manager_cleanup(ai_quic_stream_manager_t *manager);
ai_quic_stream_state_t *ai_quic_stream_manager_find(ai_quic_stream_manager_t *manager,
                                                    uint64_t stream_id);
ai_quic_stream_state_t *ai_quic_stream_manager_open_local_bidi(
    ai_quic_stream_manager_t *manager,
    uint64_t send_limit,
    uint64_t recv_limit);
ai_quic_stream_state_t *ai_quic_stream_manager_get_or_create_remote_bidi(
    ai_quic_stream_manager_t *manager,
    uint64_t stream_id,
    uint64_t send_limit,
    uint64_t recv_limit);
ai_quic_result_t ai_quic_stream_prepare_send(ai_quic_stream_state_t *stream,
                                             const uint8_t *data,
                                             size_t data_len,
                                             int send_fin,
                                             const char *request_path,
                                             const char *output_path);
ai_quic_result_t ai_quic_stream_on_receive(ai_quic_stream_state_t *stream,
                                           const ai_quic_stream_frame_t *frame);
ai_quic_result_t ai_quic_stream_consume(ai_quic_stream_state_t *stream,
                                        uint64_t bytes);
ai_quic_result_t ai_quic_crypto_stream_accept(ai_quic_packet_number_space_t *space,
                                              ai_quic_crypto_frame_t *frame);
ai_quic_result_t ai_quic_parse_invariant_header(const uint8_t *datagram,
                                                size_t datagram_len,
                                                ai_quic_packet_header_t *header);

ai_quic_result_t ai_quic_build_version_negotiation(
    const ai_quic_dispatcher_t *dispatcher,
    const ai_quic_packet_header_t *incoming,
    ai_quic_packet_t *packet);

ai_quic_result_t ai_quic_conn_init_transport(ai_quic_conn_impl_t *conn,
                                             ai_quic_tls_ctx_t *tls_ctx,
                                             ai_quic_qlog_writer_t *qlog,
                                             const char *alpn,
                                             const char *keylog_path);
ai_quic_result_t ai_quic_conn_start_client(ai_quic_conn_impl_t *conn,
                                           const char *authority,
                                           const char *request_path);
ai_quic_result_t ai_quic_conn_queue_request(ai_quic_conn_impl_t *conn,
                                            const char *request_path,
                                            const char *downloads_root);
ai_quic_result_t ai_quic_conn_on_packet(ai_quic_conn_impl_t *conn,
                                        const ai_quic_packet_t *packet,
                                        uint64_t now_ms,
                                        ai_quic_pending_datagram_t *pending,
                                        size_t *pending_count,
                                        size_t pending_capacity,
                                        const char *www_root,
                                        const char *downloads_root);
ai_quic_result_t ai_quic_conn_flush_pending(ai_quic_conn_impl_t *conn,
                                            uint64_t now_ms,
                                            ai_quic_pending_datagram_t *pending,
                                            size_t *pending_count,
                                            size_t pending_capacity,
                                            const char *www_root);

#endif
