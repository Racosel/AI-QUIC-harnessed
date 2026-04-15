#include "test_support.h"

#include <stdlib.h>
#include <string.h>

#include "ai_quic/dispatcher.h"
#include "ai_quic/log.h"
#include "common_internal.h"
#include "transport_internal.h"

static int test_version_negotiation_packet(void) {
  ai_quic_dispatcher_t *dispatcher;
  ai_quic_packet_t initial;
  ai_quic_packet_t vn;
  ai_quic_dispatch_decision_t decision;
  uint8_t datagram[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;

  dispatcher = ai_quic_dispatcher_create();
  AI_QUIC_ASSERT(dispatcher != NULL);

  memset(&initial, 0, sizeof(initial));
  initial.header.type = AI_QUIC_PACKET_TYPE_INITIAL;
  initial.header.version = 0xaaaaaaaau;
  AI_QUIC_ASSERT(ai_quic_random_cid(&initial.header.dcid, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&initial.header.scid, 8u) == AI_QUIC_OK);
  initial.frames[0].type = AI_QUIC_FRAME_PADDING;
  initial.frame_count = 1u;
  AI_QUIC_ASSERT(ai_quic_packet_encode(&initial, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  while (written < AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE) {
    datagram[written++] = 0u;
  }

  AI_QUIC_ASSERT(ai_quic_dispatcher_route_datagram(dispatcher,
                                                   datagram,
                                                   written,
                                                   &decision));
  AI_QUIC_ASSERT_EQ(decision.action, AI_QUIC_DISPATCH_VERSION_NEGOTIATION);
  AI_QUIC_ASSERT(ai_quic_build_version_negotiation(dispatcher, &decision.header, &vn) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(vn.header.version == 0u);
  AI_QUIC_ASSERT(ai_quic_cid_equal(&vn.header.dcid, &initial.header.scid));
  AI_QUIC_ASSERT(ai_quic_cid_equal(&vn.header.scid, &initial.header.dcid));
  AI_QUIC_ASSERT(vn.supported_versions_len == 1u);
  AI_QUIC_ASSERT(vn.supported_versions[0] == AI_QUIC_VERSION_V1);

  ai_quic_dispatcher_destroy(dispatcher);
  return 0;
}

static int test_wait_probe_routes_to_version_negotiation(void) {
  ai_quic_dispatcher_t *dispatcher;
  ai_quic_dispatch_decision_t decision;
  uint8_t datagram[AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE];
  ai_quic_cid_t dcid;
  ai_quic_cid_t scid;
  size_t offset;
  size_t chunk;

  dispatcher = ai_quic_dispatcher_create();
  AI_QUIC_ASSERT(dispatcher != NULL);
  AI_QUIC_ASSERT(ai_quic_random_cid(&dcid, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&scid, 8u) == AI_QUIC_OK);

  memset(datagram, 0, sizeof(datagram));
  offset = 0u;
  datagram[offset++] = 0xc0u;
  AI_QUIC_ASSERT(ai_quic_write_u32(datagram + offset,
                                   sizeof(datagram) - offset,
                                   &chunk,
                                   0x57414954u) == AI_QUIC_OK);
  offset += chunk;
  datagram[offset++] = (uint8_t)dcid.len;
  memcpy(datagram + offset, dcid.bytes, dcid.len);
  offset += dcid.len;
  datagram[offset++] = (uint8_t)scid.len;
  memcpy(datagram + offset, scid.bytes, scid.len);

  AI_QUIC_ASSERT(ai_quic_dispatcher_route_datagram(dispatcher,
                                                   datagram,
                                                   sizeof(datagram),
                                                   &decision));
  AI_QUIC_ASSERT_EQ(decision.action, AI_QUIC_DISPATCH_VERSION_NEGOTIATION);
  AI_QUIC_ASSERT(decision.header.version == 0x57414954u);
  AI_QUIC_ASSERT(ai_quic_cid_equal(&decision.header.dcid, &dcid));
  AI_QUIC_ASSERT(ai_quic_cid_equal(&decision.header.scid, &scid));

  ai_quic_dispatcher_destroy(dispatcher);
  return 0;
}

static int test_client_initial_padding(void) {
  ai_quic_endpoint_config_t config;
  ai_quic_endpoint_t *endpoint;
  uint8_t datagram[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;

  ai_quic_endpoint_config_init(&config, AI_QUIC_ENDPOINT_ROLE_CLIENT);
  config.qlog_path = "/tmp/ai_quic_unit_client.qlog";
  endpoint = ai_quic_endpoint_create(&config);
  AI_QUIC_ASSERT(endpoint != NULL);
  AI_QUIC_ASSERT(ai_quic_endpoint_start_client(endpoint, "server4:443", "/file.bin") ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_endpoint_pop_datagram(endpoint, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(written >= AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE);
  ai_quic_endpoint_destroy(endpoint);
  return 0;
}

static int test_qlog_json_seq_survives_early_read(void) {
  ai_quic_qlog_writer_t *writer;
  uint8_t buffer[2048];
  size_t read_len;
  const char *path;

  path = "/tmp/ai_quic_unit_json_seq.qlog";
  writer = ai_quic_qlog_writer_create(path, "unit-test", "client");
  AI_QUIC_ASSERT(writer != NULL);

  ai_quic_qlog_write_event(writer,
                           42u,
                           "transport",
                           "packet_sent",
                           "{\"preview\":\"001122\"}");

  AI_QUIC_ASSERT(ai_quic_read_fixture_file(path,
                                           buffer,
                                           sizeof(buffer) - 1u,
                                           &read_len) == 0);
  buffer[read_len] = '\0';

  AI_QUIC_ASSERT(read_len > 0u);
  AI_QUIC_ASSERT(buffer[0] == 0x1eu);
  AI_QUIC_ASSERT(strstr((const char *)buffer, "\"qlog_format\":\"JSON-SEQ\"") != NULL);
  AI_QUIC_ASSERT(strstr((const char *)buffer, "\"name\":\"transport:packet_sent\"") !=
                 NULL);
  AI_QUIC_ASSERT(strstr((const char *)buffer, "\"preview\":\"001122\"") != NULL);

  ai_quic_qlog_writer_destroy(writer);
  return 0;
}

static int test_pn_spaces_independent(void) {
  ai_quic_conn_t *conn;

  conn = ai_quic_conn_create(AI_QUIC_VERSION_V1, 0);
  AI_QUIC_ASSERT(conn != NULL);
  ai_quic_loss_on_packet_sent((ai_quic_conn_impl_t *)conn,
                              AI_QUIC_PN_SPACE_INITIAL,
                              1u,
                              10u);
  ai_quic_loss_on_packet_sent((ai_quic_conn_impl_t *)conn,
                              AI_QUIC_PN_SPACE_HANDSHAKE,
                              2u,
                              20u);
  ai_quic_loss_on_packet_sent((ai_quic_conn_impl_t *)conn,
                              AI_QUIC_PN_SPACE_APP_DATA,
                              3u,
                              30u);
  AI_QUIC_ASSERT(ai_quic_conn_packet_number_space(conn, AI_QUIC_PN_SPACE_INITIAL)
                     ->pto_deadline_ms !=
                 ai_quic_conn_packet_number_space(conn, AI_QUIC_PN_SPACE_HANDSHAKE)
                     ->pto_deadline_ms);
  AI_QUIC_ASSERT_EQ(ai_quic_conn_packet_number_space(conn, AI_QUIC_PN_SPACE_APP_DATA)
                        ->pto_deadline_ms,
                    0u);
  ai_quic_conn_destroy(conn);
  return 0;
}

static int test_transport_params_validation(void) {
  ai_quic_transport_params_t params;
  ai_quic_cid_t original;
  ai_quic_cid_t peer;
  uint8_t encoded[128];
  size_t written;
  ai_quic_transport_params_t decoded;

  ai_quic_transport_params_init(&params);
  AI_QUIC_ASSERT(ai_quic_random_cid(&original, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&peer, 8u) == AI_QUIC_OK);
  params.initial_source_connection_id = peer;
  params.original_destination_connection_id = original;
  params.has_original_destination_connection_id = 1;
  AI_QUIC_ASSERT(ai_quic_transport_params_encode(&params,
                                                 encoded,
                                                 sizeof(encoded),
                                                 &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_transport_params_decode(encoded, written, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_client(&decoded, &original, &peer) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_server(&decoded, &peer) ==
                 AI_QUIC_OK);
  return 0;
}

static int test_flow_control_frames_roundtrip(void) {
  ai_quic_frame_t frame;
  ai_quic_frame_t decoded;
  uint8_t buffer[64];
  size_t written;
  size_t consumed;

  memset(&frame, 0, sizeof(frame));
  frame.type = AI_QUIC_FRAME_MAX_DATA;
  frame.payload.max_data.maximum_data = 12345u;
  AI_QUIC_ASSERT(ai_quic_frame_encode(&frame, buffer, sizeof(buffer), &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_frame_decode(buffer, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(consumed, written);
  AI_QUIC_ASSERT_EQ(decoded.type, AI_QUIC_FRAME_MAX_DATA);
  AI_QUIC_ASSERT_EQ(decoded.payload.max_data.maximum_data, 12345u);

  memset(&frame, 0, sizeof(frame));
  frame.type = AI_QUIC_FRAME_MAX_STREAM_DATA;
  frame.payload.max_stream_data.stream_id = 8u;
  frame.payload.max_stream_data.maximum_stream_data = 65536u;
  AI_QUIC_ASSERT(ai_quic_frame_encode(&frame, buffer, sizeof(buffer), &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_frame_decode(buffer, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(decoded.type, AI_QUIC_FRAME_MAX_STREAM_DATA);
  AI_QUIC_ASSERT_EQ(decoded.payload.max_stream_data.stream_id, 8u);
  AI_QUIC_ASSERT_EQ(decoded.payload.max_stream_data.maximum_stream_data, 65536u);

  memset(&frame, 0, sizeof(frame));
  frame.type = AI_QUIC_FRAME_DATA_BLOCKED;
  frame.payload.data_blocked.limit = 777u;
  AI_QUIC_ASSERT(ai_quic_frame_encode(&frame, buffer, sizeof(buffer), &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_frame_decode(buffer, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(decoded.type, AI_QUIC_FRAME_DATA_BLOCKED);
  AI_QUIC_ASSERT_EQ(decoded.payload.data_blocked.limit, 777u);

  memset(&frame, 0, sizeof(frame));
  frame.type = AI_QUIC_FRAME_STREAM_DATA_BLOCKED;
  frame.payload.stream_data_blocked.stream_id = 4u;
  frame.payload.stream_data_blocked.limit = 4096u;
  AI_QUIC_ASSERT(ai_quic_frame_encode(&frame, buffer, sizeof(buffer), &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_frame_decode(buffer, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(decoded.type, AI_QUIC_FRAME_STREAM_DATA_BLOCKED);
  AI_QUIC_ASSERT_EQ(decoded.payload.stream_data_blocked.stream_id, 4u);
  AI_QUIC_ASSERT_EQ(decoded.payload.stream_data_blocked.limit, 4096u);

  memset(&frame, 0, sizeof(frame));
  frame.type = AI_QUIC_FRAME_ACK;
  frame.payload.ack.largest_acked = 5u;
  frame.payload.ack.first_ack_range = 0u;
  frame.payload.ack.ack_range_count = 1u;
  frame.payload.ack.ack_ranges[0].gap = 1u;
  frame.payload.ack.ack_ranges[0].ack_range = 2u;
  AI_QUIC_ASSERT(ai_quic_frame_encode(&frame, buffer, sizeof(buffer), &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_frame_decode(buffer, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(decoded.type, AI_QUIC_FRAME_ACK);
  AI_QUIC_ASSERT_EQ(decoded.payload.ack.largest_acked, 5u);
  AI_QUIC_ASSERT_EQ(decoded.payload.ack.first_ack_range, 0u);
  AI_QUIC_ASSERT_EQ(decoded.payload.ack.ack_range_count, 1u);
  AI_QUIC_ASSERT_EQ(decoded.payload.ack.ack_ranges[0].gap, 1u);
  AI_QUIC_ASSERT_EQ(decoded.payload.ack.ack_ranges[0].ack_range, 2u);
  return 0;
}

static int test_ack_range_generation(void) {
  ai_quic_packet_number_space_t space;
  ai_quic_frame_t frame;

  AI_QUIC_ASSERT(ai_quic_pn_space_init(&space, AI_QUIC_PN_SPACE_APP_DATA) == AI_QUIC_OK);
  ai_quic_pn_space_on_packet_received(&space, 0u);
  ai_quic_pn_space_on_packet_received(&space, 1u);
  ai_quic_pn_space_on_packet_received(&space, 2u);
  ai_quic_pn_space_on_packet_received(&space, 5u);

  memset(&frame, 0, sizeof(frame));
  ai_quic_build_ack_frame(&space, &frame);
  AI_QUIC_ASSERT_EQ(frame.type, AI_QUIC_FRAME_ACK);
  AI_QUIC_ASSERT_EQ(frame.payload.ack.largest_acked, 5u);
  AI_QUIC_ASSERT_EQ(frame.payload.ack.first_ack_range, 0u);
  AI_QUIC_ASSERT_EQ(frame.payload.ack.ack_range_count, 1u);
  AI_QUIC_ASSERT_EQ(frame.payload.ack.ack_ranges[0].gap, 1u);
  AI_QUIC_ASSERT_EQ(frame.payload.ack.ack_ranges[0].ack_range, 2u);
  return 0;
}

static int test_stream_reassembly_out_of_order(void) {
  ai_quic_stream_manager_t manager;
  ai_quic_stream_state_t *stream;
  ai_quic_stream_frame_t frame;
  static const uint8_t kWorld[] = "world";
  static const uint8_t kHello[] = "hello";

  ai_quic_stream_manager_init(&manager);
  stream = ai_quic_stream_manager_open_local_bidi(&manager, 65536u, 65536u);
  AI_QUIC_ASSERT(stream != NULL);

  memset(&frame, 0, sizeof(frame));
  frame.stream_id = stream->stream_id;
  frame.offset = 5u;
  frame.data_len = sizeof(kWorld) - 1u;
  memcpy(frame.data, kWorld, frame.data_len);
  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(stream->recv_contiguous_end, 0u);

  frame.offset = 0u;
  frame.data_len = sizeof(kHello) - 1u;
  memcpy(frame.data, kHello, frame.data_len);
  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(stream->recv_contiguous_end, 10u);
  AI_QUIC_ASSERT(memcmp(stream->recv_data, "helloworld", 10u) == 0);

  ai_quic_stream_manager_cleanup(&manager);
  return 0;
}

static int test_stream_overlap_duplicate_and_final_size(void) {
  ai_quic_stream_manager_t manager;
  ai_quic_stream_state_t *stream;
  ai_quic_stream_frame_t frame;

  ai_quic_stream_manager_init(&manager);
  stream = ai_quic_stream_manager_open_local_bidi(&manager, 65536u, 65536u);
  AI_QUIC_ASSERT(stream != NULL);

  memset(&frame, 0, sizeof(frame));
  frame.stream_id = stream->stream_id;
  frame.offset = 0u;
  frame.data_len = 5u;
  frame.fin = 1;
  memcpy(frame.data, "hello", 5u);
  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_OK);
  AI_QUIC_ASSERT(stream->recv_fin);
  AI_QUIC_ASSERT(stream->final_size_known);
  AI_QUIC_ASSERT_EQ(stream->final_size, 5u);

  frame.offset = 0u;
  frame.data_len = 5u;
  frame.fin = 1;
  memcpy(frame.data, "hello", 5u);
  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_OK);

  frame.offset = 3u;
  frame.data_len = 2u;
  frame.fin = 0;
  memcpy(frame.data, "lo", 2u);
  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_OK);

  frame.offset = 5u;
  frame.data_len = 1u;
  frame.fin = 0;
  frame.data[0] = '!';
  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_ERROR);

  ai_quic_stream_manager_cleanup(&manager);
  return 0;
}

static int test_stream_flow_control_updates_on_consume(void) {
  ai_quic_stream_manager_t manager;
  ai_quic_stream_state_t *stream;

  ai_quic_stream_manager_init(&manager);
  stream = ai_quic_stream_manager_open_local_bidi(&manager, 1024u, 65536u);
  AI_QUIC_ASSERT(stream != NULL);
  stream->recv_contiguous_end = 40000u;
  AI_QUIC_ASSERT(ai_quic_stream_consume(stream, 32768u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(stream->flow.update_pending);
  AI_QUIC_ASSERT_EQ(stream->flow.recv_limit,
                    32768u + 65536u + AI_QUIC_STREAM_RECV_LIMIT_RESERVE);
  AI_QUIC_ASSERT_EQ(stream->flow.recv_consumed, 32768u);

  ai_quic_flow_controller_set_send_limit(&stream->flow, 131072u);
  AI_QUIC_ASSERT_EQ(stream->flow.send_limit, 131072u);
  ai_quic_flow_controller_set_send_limit(&stream->flow, 1024u);
  AI_QUIC_ASSERT_EQ(stream->flow.send_limit, 131072u);

  ai_quic_stream_manager_cleanup(&manager);
  return 0;
}

static int test_stream_before_1rtt_rejected(void) {
  ai_quic_conn_impl_t *conn;
  ai_quic_packet_t packet;
  ai_quic_pending_datagram_t pending[4];
  size_t pending_count;

  conn = (ai_quic_conn_impl_t *)ai_quic_conn_create(AI_QUIC_VERSION_V1, 1);
  AI_QUIC_ASSERT(conn != NULL);
  AI_QUIC_ASSERT(ai_quic_conn_init_transport(conn, ai_quic_tls_ctx_create(NULL), NULL, "hq-interop", NULL) ==
                 AI_QUIC_OK);
  memset(&packet, 0, sizeof(packet));
  packet.header.type = AI_QUIC_PACKET_TYPE_ONE_RTT;
  packet.header.packet_length = 20u;
  packet.frames[0].type = AI_QUIC_FRAME_STREAM;
  packet.frames[0].payload.stream.stream_id = 0u;
  packet.frames[0].payload.stream.data_len = 1u;
  packet.frames[0].payload.stream.data[0] = 'x';
  packet.frame_count = 1u;
  pending_count = 0u;
  AI_QUIC_ASSERT(ai_quic_conn_on_packet(conn,
                                        &packet,
                                        1u,
                                        pending,
                                        &pending_count,
                                        4u,
                                        "/tmp",
                                        "/tmp") == AI_QUIC_ERROR);
  ai_quic_conn_destroy((ai_quic_conn_t *)conn);
  return 0;
}

int main(void) {
  ai_quic_log_set_level(AI_QUIC_LOG_ERROR);
  AI_QUIC_ASSERT(test_version_negotiation_packet() == 0);
  AI_QUIC_ASSERT(test_wait_probe_routes_to_version_negotiation() == 0);
  AI_QUIC_ASSERT(test_client_initial_padding() == 0);
  AI_QUIC_ASSERT(test_qlog_json_seq_survives_early_read() == 0);
  AI_QUIC_ASSERT(test_pn_spaces_independent() == 0);
  AI_QUIC_ASSERT(test_transport_params_validation() == 0);
  AI_QUIC_ASSERT(test_flow_control_frames_roundtrip() == 0);
  AI_QUIC_ASSERT(test_stream_reassembly_out_of_order() == 0);
  AI_QUIC_ASSERT(test_stream_overlap_duplicate_and_final_size() == 0);
  AI_QUIC_ASSERT(test_stream_flow_control_updates_on_consume() == 0);
  AI_QUIC_ASSERT(test_stream_before_1rtt_rejected() == 0);
  AI_QUIC_ASSERT(test_ack_range_generation() == 0);
  puts("ai_quic_unit_test: ok");
  return 0;
}
