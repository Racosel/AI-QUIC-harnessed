#include "test_support.h"

#include <stdlib.h>
#include <string.h>

#include "ai_quic/dispatcher.h"
#include "ai_quic/log.h"
#include "common_internal.h"
#include "transport_internal.h"

static void build_ping_packet(ai_quic_packet_t *packet,
                              ai_quic_packet_type_t type,
                              uint64_t packet_number,
                              size_t packet_length) {
  memset(packet, 0, sizeof(*packet));
  packet->header.type = type;
  packet->header.packet_number = packet_number;
  packet->header.packet_length = packet_length;
  packet->frames[0].type = AI_QUIC_FRAME_PING;
  packet->frame_count = 1u;
}

static void build_stream_packet(ai_quic_packet_t *packet,
                                uint64_t packet_number,
                                uint64_t stream_id,
                                uint64_t offset,
                                size_t data_len,
                                int fin) {
  size_t i;

  memset(packet, 0, sizeof(*packet));
  packet->header.type = AI_QUIC_PACKET_TYPE_ONE_RTT;
  packet->header.packet_number = packet_number;
  packet->header.packet_length = data_len + 64u;
  packet->frames[0].type = AI_QUIC_FRAME_STREAM;
  packet->frames[0].payload.stream.stream_id = stream_id;
  packet->frames[0].payload.stream.offset = offset;
  packet->frames[0].payload.stream.data_len = data_len;
  packet->frames[0].payload.stream.fin = fin;
  for (i = 0u; i < data_len; ++i) {
    packet->frames[0].payload.stream.data[i] = (uint8_t)('a' + ((offset + i) % 26u));
  }
  packet->frame_count = 1u;
}

static ai_quic_conn_impl_t *create_recovery_test_conn(ai_quic_stream_state_t **stream_out,
                                                      size_t data_len,
                                                      int send_fin) {
  ai_quic_conn_impl_t *conn;
  ai_quic_stream_state_t *stream;
  uint8_t data[4096];
  size_t i;

  conn = (ai_quic_conn_impl_t *)ai_quic_conn_create(AI_QUIC_VERSION_V1, 1);
  if (conn == NULL) {
    return NULL;
  }
  conn->handshake_confirmed = 1;
  conn->can_send_1rtt = 1;
  conn->peer_transport_params.initial_max_data = 65536u;
  conn->conn_flow.send_limit = 65536u;

  stream = ai_quic_stream_manager_open_local_bidi(&conn->streams, 65536u, 65536u);
  if (stream == NULL) {
    ai_quic_conn_destroy((ai_quic_conn_t *)conn);
    return NULL;
  }
  for (i = 0u; i < data_len && i < sizeof(data); ++i) {
    data[i] = (uint8_t)('a' + (i % 26u));
  }
  if (ai_quic_stream_prepare_send(stream, data, data_len, send_fin, NULL, NULL) != AI_QUIC_OK) {
    ai_quic_conn_destroy((ai_quic_conn_t *)conn);
    return NULL;
  }
  if (stream_out != NULL) {
    *stream_out = stream;
  }
  return conn;
}

static int test_version_negotiation_packet(void) {
  ai_quic_dispatcher_t *dispatcher;
  ai_quic_packet_t initial;
  ai_quic_packet_t vn;
  ai_quic_dispatch_decision_t decision;
  uint8_t datagram[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  size_t chunk;

  dispatcher = ai_quic_dispatcher_create();
  AI_QUIC_ASSERT(dispatcher != NULL);

  memset(&initial, 0, sizeof(initial));
  initial.header.type = AI_QUIC_PACKET_TYPE_INITIAL;
  initial.header.version = AI_QUIC_VERSION_V1;
  AI_QUIC_ASSERT(ai_quic_random_cid(&initial.header.dcid, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&initial.header.scid, 8u) == AI_QUIC_OK);
  initial.frames[0].type = AI_QUIC_FRAME_PADDING;
  initial.frame_count = 1u;
  AI_QUIC_ASSERT(ai_quic_packet_encode(&initial, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_write_u32(datagram + 1u,
                                   sizeof(datagram) - 1u,
                                   &chunk,
                                   0xaaaaaaaau) == AI_QUIC_OK);
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
  AI_QUIC_ASSERT(vn.supported_versions_len == 2u);
  AI_QUIC_ASSERT(vn.supported_versions[0] == AI_QUIC_VERSION_V1);
  AI_QUIC_ASSERT(vn.supported_versions[1] == AI_QUIC_VERSION_V2);

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
  ai_quic_packet_t packet;

  conn = ai_quic_conn_create(AI_QUIC_VERSION_V1, 0);
  AI_QUIC_ASSERT(conn != NULL);
  build_ping_packet(&packet, AI_QUIC_PACKET_TYPE_INITIAL, 1u, 120u);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent((ai_quic_conn_impl_t *)conn,
                                             AI_QUIC_PN_SPACE_INITIAL,
                                             &packet,
                                             packet.header.packet_length,
                                             10u) == AI_QUIC_OK);
  build_ping_packet(&packet, AI_QUIC_PACKET_TYPE_HANDSHAKE, 2u, 120u);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent((ai_quic_conn_impl_t *)conn,
                                             AI_QUIC_PN_SPACE_HANDSHAKE,
                                             &packet,
                                             packet.header.packet_length,
                                             20u) == AI_QUIC_OK);
  build_ping_packet(&packet, AI_QUIC_PACKET_TYPE_ONE_RTT, 3u, 120u);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent((ai_quic_conn_impl_t *)conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             packet.header.packet_length,
                                             30u) == AI_QUIC_OK);
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
  uint8_t encoded[256];
  size_t written;
  ai_quic_transport_params_t decoded;

  ai_quic_transport_params_init(&params);
  AI_QUIC_ASSERT(ai_quic_random_cid(&original, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&peer, 8u) == AI_QUIC_OK);
  params.initial_source_connection_id = peer;
  params.original_destination_connection_id = original;
  params.has_original_destination_connection_id = 1;
  params.version_information.present = 1;
  params.version_information.chosen_version = AI_QUIC_VERSION_V1;
  params.version_information.available_versions[0] = AI_QUIC_VERSION_V2;
  params.version_information.available_versions[1] = AI_QUIC_VERSION_V1;
  params.version_information.available_versions_len = 2u;
  AI_QUIC_ASSERT(ai_quic_transport_params_encode(&params,
                                                 encoded,
                                                 sizeof(encoded),
                                                 &written) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_transport_params_decode(encoded, written, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_client(&decoded, &original, &peer) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_server(&decoded, &peer) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(decoded.version_information.present);
  AI_QUIC_ASSERT_EQ(decoded.version_information.chosen_version, AI_QUIC_VERSION_V1);
  AI_QUIC_ASSERT_EQ(decoded.version_information.available_versions_len, 2u);
  AI_QUIC_ASSERT_EQ(decoded.version_information.available_versions[0], AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT_EQ(decoded.version_information.available_versions[1], AI_QUIC_VERSION_V1);
  return 0;
}

static int test_transport_params_reject_malformed_version_information(void) {
  uint8_t encoded[64];
  ai_quic_transport_params_t params;
  size_t offset;
  size_t chunk;

  offset = 0u;
  AI_QUIC_ASSERT(ai_quic_varint_write(encoded + offset,
                                      sizeof(encoded) - offset,
                                      &chunk,
                                      0x11u) == AI_QUIC_OK);
  offset += chunk;
  AI_QUIC_ASSERT(ai_quic_varint_write(encoded + offset,
                                      sizeof(encoded) - offset,
                                      &chunk,
                                      5u) == AI_QUIC_OK);
  offset += chunk;
  AI_QUIC_ASSERT(ai_quic_write_u32(encoded + offset,
                                   sizeof(encoded) - offset,
                                   &chunk,
                                   AI_QUIC_VERSION_V1) == AI_QUIC_OK);
  offset += chunk;
  encoded[offset++] = 0xffu;

  AI_QUIC_ASSERT(ai_quic_transport_params_decode(encoded, offset, &params) == AI_QUIC_ERROR);
  return 0;
}

static int test_version_catalog_v2_retry_and_reserved(void) {
  const ai_quic_version_ops_t *v1;
  const ai_quic_version_ops_t *v2;
  uint8_t first_byte;

  v1 = ai_quic_version_ops_find(AI_QUIC_VERSION_V1);
  v2 = ai_quic_version_ops_find(AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT(v1 != NULL);
  AI_QUIC_ASSERT(v2 != NULL);
  AI_QUIC_ASSERT_EQ(v2->wire_version, AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT_EQ(v2->initial_type_bits, 0x01u);
  AI_QUIC_ASSERT_EQ(v2->zero_rtt_type_bits, 0x02u);
  AI_QUIC_ASSERT_EQ(v2->handshake_type_bits, 0x03u);
  AI_QUIC_ASSERT_EQ(v2->retry_type_bits, 0x00u);
  AI_QUIC_ASSERT_EQ(v2->initial_salt[0], 0x0du);
  AI_QUIC_ASSERT_EQ(v2->retry_integrity_secret[0], 0xc4u);
  AI_QUIC_ASSERT_EQ(v2->retry_integrity_key[0], 0x8fu);
  AI_QUIC_ASSERT_EQ(v2->retry_integrity_nonce[0], 0xd8u);
  AI_QUIC_ASSERT(strcmp(v2->key_label, "quicv2 key") == 0);
  AI_QUIC_ASSERT(strcmp(v2->iv_label, "quicv2 iv") == 0);
  AI_QUIC_ASSERT(strcmp(v2->hp_label, "quicv2 hp") == 0);
  AI_QUIC_ASSERT(strcmp(v2->ku_label, "quicv2 ku") == 0);
  AI_QUIC_ASSERT(v1->retry_integrity_key[0] != v2->retry_integrity_key[0]);
  AI_QUIC_ASSERT(ai_quic_version_reserved(0x1a2a3a4au));
  AI_QUIC_ASSERT(!ai_quic_version_supported(0x1a2a3a4au));
  AI_QUIC_ASSERT(ai_quic_version_compatible(AI_QUIC_VERSION_V1, AI_QUIC_VERSION_V2));
  AI_QUIC_ASSERT(ai_quic_version_compatible(AI_QUIC_VERSION_V2, AI_QUIC_VERSION_V1));
  AI_QUIC_ASSERT(!ai_quic_version_compatible(AI_QUIC_VERSION_V1, 0x1a2a3a4au));
  AI_QUIC_ASSERT(ai_quic_version_encode_long_header_first_byte(
                     AI_QUIC_VERSION_V2, AI_QUIC_PACKET_TYPE_INITIAL, 4u, &first_byte) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ((uint8_t)((first_byte >> 4u) & 0x03u), 0x01u);
  return 0;
}

static int test_version_information_error_code_split(void) {
  ai_quic_version_information_t client_vi;
  ai_quic_version_information_t server_vi;
  uint64_t error_code;

  memset(&client_vi, 0, sizeof(client_vi));
  client_vi.present = 1;
  client_vi.chosen_version = AI_QUIC_VERSION_V1;
  client_vi.available_versions[0] = AI_QUIC_VERSION_V2;
  client_vi.available_versions_len = 1u;
  error_code = 0u;
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_client_version_information(
                     &client_vi, AI_QUIC_VERSION_V1, 1, &error_code) == AI_QUIC_ERROR);
  AI_QUIC_ASSERT_EQ(error_code, AI_QUIC_TRANSPORT_ERROR_TRANSPORT_PARAMETER);

  client_vi.available_versions[1] = AI_QUIC_VERSION_V1;
  client_vi.available_versions_len = 2u;
  error_code = 0u;
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_client_version_information(
                     &client_vi, AI_QUIC_VERSION_V2, 1, &error_code) == AI_QUIC_ERROR);
  AI_QUIC_ASSERT_EQ(error_code, AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION);

  memset(&server_vi, 0, sizeof(server_vi));
  server_vi.present = 1;
  server_vi.chosen_version = AI_QUIC_VERSION_V2;
  client_vi.available_versions[0] = AI_QUIC_VERSION_V1;
  client_vi.available_versions_len = 1u;
  error_code = 0u;
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_server_version_information(
                     &server_vi,
                     &client_vi,
                     AI_QUIC_VERSION_V2,
                     1,
                     &error_code) == AI_QUIC_ERROR);
  AI_QUIC_ASSERT_EQ(error_code, AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION);

  client_vi.available_versions[0] = AI_QUIC_VERSION_V2;
  client_vi.available_versions[1] = AI_QUIC_VERSION_V1;
  client_vi.available_versions_len = 2u;
  server_vi.chosen_version = AI_QUIC_VERSION_V1;
  error_code = 0u;
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_server_version_information(
                     &server_vi,
                     &client_vi,
                     AI_QUIC_VERSION_V2,
                     1,
                     &error_code) == AI_QUIC_ERROR);
  AI_QUIC_ASSERT_EQ(error_code, AI_QUIC_TRANSPORT_ERROR_VERSION_NEGOTIATION);

  server_vi.chosen_version = AI_QUIC_VERSION_V2;
  error_code = 0u;
  AI_QUIC_ASSERT(ai_quic_transport_params_validate_server_version_information(
                     &server_vi,
                     &client_vi,
                     AI_QUIC_VERSION_V2,
                     1,
                     &error_code) == AI_QUIC_OK);
  return 0;
}

static int test_tls_cipher_policy_chacha20_fake_secret(void) {
  ai_quic_tls_ctx_t *tls_ctx;
  ai_quic_tls_session_t *session;
  uint8_t secret[64];
  size_t secret_len;
  uint32_t cipher_suite;

  AI_QUIC_ASSERT(strcmp(ai_quic_tls_cipher_policy_name(
                            AI_QUIC_TLS_CIPHER_POLICY_DEFAULT),
                        "default") == 0);
  AI_QUIC_ASSERT(strcmp(ai_quic_tls_cipher_policy_name(
                            AI_QUIC_TLS_CIPHER_POLICY_CHACHA20_ONLY),
                        "chacha20-only") == 0);
  AI_QUIC_ASSERT(ai_quic_tls_cipher_policy_from_name("chacha20") ==
                 AI_QUIC_TLS_CIPHER_POLICY_CHACHA20_ONLY);
  AI_QUIC_ASSERT(ai_quic_tls_cipher_policy_from_name("chacha20-only") ==
                 AI_QUIC_TLS_CIPHER_POLICY_CHACHA20_ONLY);
  AI_QUIC_ASSERT(ai_quic_tls_cipher_policy_from_name("unknown") ==
                 AI_QUIC_TLS_CIPHER_POLICY_DEFAULT);

  tls_ctx = ai_quic_tls_ctx_create(NULL);
  AI_QUIC_ASSERT(tls_ctx != NULL);
  session = ai_quic_tls_session_create(tls_ctx,
                                       0,
                                       "hq-interop",
                                       NULL,
                                       AI_QUIC_TLS_CIPHER_POLICY_CHACHA20_ONLY);
  AI_QUIC_ASSERT(session != NULL);
  AI_QUIC_ASSERT(ai_quic_tls_session_get_packet_secret(session,
                                                       AI_QUIC_ENCRYPTION_HANDSHAKE,
                                                       1,
                                                       &cipher_suite,
                                                       secret,
                                                       &secret_len,
                                                       sizeof(secret)) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ((cipher_suite & 0xffffu), 0x1303u);
  AI_QUIC_ASSERT(secret_len > 0u);
  AI_QUIC_ASSERT(strcmp(ai_quic_tls_cipher_suite_name(cipher_suite),
                        "TLS_CHACHA20_POLY1305_SHA256") == 0);

  ai_quic_tls_session_destroy(session);
  ai_quic_tls_ctx_destroy(tls_ctx);
  return 0;
}

static int test_v2_long_header_roundtrip(void) {
  ai_quic_packet_t packet;
  ai_quic_packet_t decoded;
  uint8_t datagram[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t first_byte;
  size_t written;
  size_t consumed;

  memset(&packet, 0, sizeof(packet));
  packet.header.type = AI_QUIC_PACKET_TYPE_INITIAL;
  packet.header.version = AI_QUIC_VERSION_V1;
  packet.header.packet_number = 7u;
  AI_QUIC_ASSERT(ai_quic_random_cid(&packet.header.dcid, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&packet.header.scid, 8u) == AI_QUIC_OK);
  packet.frames[0].type = AI_QUIC_FRAME_PADDING;
  packet.frame_count = 1u;
  AI_QUIC_ASSERT(ai_quic_version_encode_long_header_first_byte(
                     AI_QUIC_VERSION_V2, AI_QUIC_PACKET_TYPE_INITIAL, 4u, &first_byte) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ((uint8_t)((first_byte >> 4u) & 0x03u), 0x01u);
  AI_QUIC_ASSERT(ai_quic_packet_encode(&packet, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  datagram[0] = first_byte;
  AI_QUIC_ASSERT(ai_quic_write_u32(datagram + 1u,
                                   sizeof(datagram) - 1u,
                                   &consumed,
                                   AI_QUIC_VERSION_V2) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_packet_decode(datagram, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(consumed, written);
  AI_QUIC_ASSERT_EQ(decoded.header.version, AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT_EQ(decoded.header.type, AI_QUIC_PACKET_TYPE_INITIAL);
  AI_QUIC_ASSERT_EQ(decoded.header.packet_number, 7u);

  memset(&packet, 0, sizeof(packet));
  packet.header.type = AI_QUIC_PACKET_TYPE_HANDSHAKE;
  packet.header.version = AI_QUIC_VERSION_V1;
  packet.header.packet_number = 9u;
  AI_QUIC_ASSERT(ai_quic_random_cid(&packet.header.dcid, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&packet.header.scid, 8u) == AI_QUIC_OK);
  packet.frames[0].type = AI_QUIC_FRAME_PADDING;
  packet.frame_count = 1u;
  AI_QUIC_ASSERT(ai_quic_version_encode_long_header_first_byte(
                     AI_QUIC_VERSION_V2, AI_QUIC_PACKET_TYPE_HANDSHAKE, 4u, &first_byte) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ((uint8_t)((first_byte >> 4u) & 0x03u), 0x03u);
  AI_QUIC_ASSERT(ai_quic_packet_encode(&packet, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  datagram[0] = first_byte;
  AI_QUIC_ASSERT(ai_quic_write_u32(datagram + 1u,
                                   sizeof(datagram) - 1u,
                                   &consumed,
                                   AI_QUIC_VERSION_V2) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_packet_decode(datagram, written, &consumed, &decoded) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(decoded.header.version, AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT_EQ(decoded.header.type, AI_QUIC_PACKET_TYPE_HANDSHAKE);
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

static int test_stream_flow_control_updates_on_receive(void) {
  ai_quic_stream_manager_t manager;
  ai_quic_stream_state_t *stream;
  ai_quic_stream_frame_t frame;

  ai_quic_stream_manager_init(&manager);
  stream = ai_quic_stream_manager_open_local_bidi(&manager, 1024u, 65536u);
  AI_QUIC_ASSERT(stream != NULL);

  memset(&frame, 0, sizeof(frame));
  frame.stream_id = stream->stream_id;
  frame.offset = 65000u;
  frame.data_len = 256u;
  memcpy(frame.data, "early-window-refresh", sizeof("early-window-refresh") - 1u);

  AI_QUIC_ASSERT(ai_quic_stream_on_receive(stream, &frame) == AI_QUIC_OK);
  AI_QUIC_ASSERT(stream->flow.update_pending);
  AI_QUIC_ASSERT_EQ(stream->flow.highest_received, 65256u);
  AI_QUIC_ASSERT_EQ(stream->flow.recv_limit,
                    65256u + 65536u + AI_QUIC_STREAM_RECV_LIMIT_RESERVE);

  ai_quic_stream_manager_cleanup(&manager);
  return 0;
}

static int test_ack_gap_preserves_bytes_in_flight(void) {
  ai_quic_conn_impl_t *conn;
  ai_quic_stream_state_t *stream;
  ai_quic_packet_t packet;
  ai_quic_ack_frame_t ack;

  conn = create_recovery_test_conn(&stream, 3600u, 0);
  AI_QUIC_ASSERT(conn != NULL);
  conn->rtt_state.latest_rtt_ms = 1000u;
  conn->rtt_state.smoothed_rtt_ms = 1000u;
  conn->rtt_state.rttvar_ms = 500u;
  conn->rtt_state.min_rtt_ms = 1000u;

  build_stream_packet(&packet, 1u, stream->stream_id, 0u, 1200u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             1250u,
                                             10u) == AI_QUIC_OK);
  build_stream_packet(&packet, 2u, stream->stream_id, 1200u, 1200u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             1250u,
                                             20u) == AI_QUIC_OK);
  build_stream_packet(&packet, 3u, stream->stream_id, 2400u, 1200u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             1250u,
                                             30u) == AI_QUIC_OK);

  memset(&ack, 0, sizeof(ack));
  ack.largest_acked = 3u;
  ack.first_ack_range = 0u;
  ack.ack_range_count = 1u;
  ack.ack_ranges[0].gap = 0u;
  ack.ack_ranges[0].ack_range = 0u;
  ai_quic_loss_on_ack_received(conn, AI_QUIC_PN_SPACE_APP_DATA, &ack, 100u, NULL);

  AI_QUIC_ASSERT_EQ(conn->packet_spaces[AI_QUIC_PN_SPACE_APP_DATA].bytes_in_flight, 1250u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.count, 2u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[0].start, 0u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[0].end, 1200u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[1].start, 2400u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[1].end, 3600u);
  AI_QUIC_ASSERT_EQ(stream->lost_ranges.count, 0u);

  ai_quic_conn_destroy((ai_quic_conn_t *)conn);
  return 0;
}

static int test_packet_threshold_marks_only_eligible_loss(void) {
  ai_quic_conn_impl_t *conn;
  ai_quic_stream_state_t *stream;
  ai_quic_packet_t packet;
  ai_quic_ack_frame_t ack;

  conn = create_recovery_test_conn(&stream, 400u, 0);
  AI_QUIC_ASSERT(conn != NULL);
  conn->rtt_state.latest_rtt_ms = 1000u;
  conn->rtt_state.smoothed_rtt_ms = 1000u;
  conn->rtt_state.rttvar_ms = 500u;
  conn->rtt_state.min_rtt_ms = 1000u;

  build_stream_packet(&packet, 1u, stream->stream_id, 0u, 100u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             120u,
                                             10u) == AI_QUIC_OK);
  build_stream_packet(&packet, 2u, stream->stream_id, 100u, 100u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             120u,
                                             20u) == AI_QUIC_OK);
  build_stream_packet(&packet, 3u, stream->stream_id, 200u, 100u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             120u,
                                             30u) == AI_QUIC_OK);
  build_stream_packet(&packet, 4u, stream->stream_id, 300u, 100u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             120u,
                                             40u) == AI_QUIC_OK);

  memset(&ack, 0, sizeof(ack));
  ack.largest_acked = 4u;
  ack.first_ack_range = 0u;
  ai_quic_loss_on_ack_received(conn, AI_QUIC_PN_SPACE_APP_DATA, &ack, 50u, NULL);

  AI_QUIC_ASSERT_EQ(conn->packet_spaces[AI_QUIC_PN_SPACE_APP_DATA].bytes_in_flight, 240u);
  AI_QUIC_ASSERT_EQ(stream->lost_ranges.count, 1u);
  AI_QUIC_ASSERT_EQ(stream->lost_ranges.ranges[0].start, 0u);
  AI_QUIC_ASSERT_EQ(stream->lost_ranges.ranges[0].end, 100u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.count, 1u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[0].start, 300u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[0].end, 400u);

  ai_quic_conn_destroy((ai_quic_conn_t *)conn);
  return 0;
}

static int test_lost_ranges_cleared_by_retransmit_ack(void) {
  ai_quic_conn_impl_t *conn;
  ai_quic_stream_state_t *stream;
  ai_quic_packet_t packet;
  ai_quic_ack_frame_t ack;

  conn = create_recovery_test_conn(&stream, 100u, 0);
  AI_QUIC_ASSERT(conn != NULL);
  AI_QUIC_ASSERT(ai_quic_stream_mark_lost(stream, 0u, 100u, 0) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(stream->lost_ranges.count, 1u);

  build_stream_packet(&packet, 2u, stream->stream_id, 0u, 100u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             120u,
                                             20u) == AI_QUIC_OK);

  memset(&ack, 0, sizeof(ack));
  ack.largest_acked = 2u;
  ack.first_ack_range = 0u;
  ai_quic_loss_on_ack_received(conn, AI_QUIC_PN_SPACE_APP_DATA, &ack, 40u, NULL);

  AI_QUIC_ASSERT_EQ(stream->lost_ranges.count, 0u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.count, 1u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[0].start, 0u);
  AI_QUIC_ASSERT_EQ(stream->acked_ranges.ranges[0].end, 100u);

  ai_quic_conn_destroy((ai_quic_conn_t *)conn);
  return 0;
}

static int test_lost_range_retransmit_prefers_earliest_gap(void) {
  ai_quic_stream_manager_t manager;
  ai_quic_stream_state_t *stream;
  uint8_t data[64];
  size_t i;
  uint64_t offset;
  size_t chunk_len;
  int fin;

  ai_quic_stream_manager_init(&manager);
  stream = ai_quic_stream_manager_open_local_bidi(&manager, 8192u, 8192u);
  AI_QUIC_ASSERT(stream != NULL);
  for (i = 0u; i < sizeof(data); ++i) {
    data[i] = (uint8_t)('a' + (i % 26u));
  }
  AI_QUIC_ASSERT(ai_quic_stream_prepare_send(stream,
                                             data,
                                             sizeof(data),
                                             1,
                                             NULL,
                                             NULL) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_stream_mark_lost(stream, 10u, 18u, 0) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_stream_mark_lost(stream, 40u, 52u, 0) == AI_QUIC_OK);

  AI_QUIC_ASSERT(ai_quic_stream_pop_lost_range(stream, 4u, &offset, &chunk_len, &fin));
  AI_QUIC_ASSERT_EQ(offset, 10u);
  AI_QUIC_ASSERT_EQ(chunk_len, 4u);
  AI_QUIC_ASSERT(!fin);

  AI_QUIC_ASSERT(ai_quic_stream_pop_lost_range(stream, 16u, &offset, &chunk_len, &fin));
  AI_QUIC_ASSERT_EQ(offset, 14u);
  AI_QUIC_ASSERT_EQ(chunk_len, 4u);
  AI_QUIC_ASSERT(!fin);

  AI_QUIC_ASSERT(ai_quic_stream_pop_lost_range(stream, 16u, &offset, &chunk_len, &fin));
  AI_QUIC_ASSERT_EQ(offset, 40u);
  AI_QUIC_ASSERT_EQ(chunk_len, 12u);
  AI_QUIC_ASSERT(!fin);

  ai_quic_stream_manager_cleanup(&manager);
  return 0;
}

static int test_pto_backoff_resets_on_new_ack(void) {
  ai_quic_conn_impl_t *conn;
  ai_quic_stream_state_t *stream;
  ai_quic_packet_t packet;
  ai_quic_ack_frame_t ack;

  conn = create_recovery_test_conn(&stream, 100u, 0);
  AI_QUIC_ASSERT(conn != NULL);
  build_stream_packet(&packet, 1u, stream->stream_id, 0u, 100u, 0);
  AI_QUIC_ASSERT(ai_quic_loss_on_packet_sent(conn,
                                             AI_QUIC_PN_SPACE_APP_DATA,
                                             &packet,
                                             120u,
                                             10u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_loss_on_timeout(conn, AI_QUIC_PN_SPACE_APP_DATA, 1100u) == 1);
  AI_QUIC_ASSERT_EQ(conn->loss_state[AI_QUIC_PN_SPACE_APP_DATA].pto_count, 1u);

  memset(&ack, 0, sizeof(ack));
  ack.largest_acked = 1u;
  ack.first_ack_range = 0u;
  ai_quic_loss_on_ack_received(conn, AI_QUIC_PN_SPACE_APP_DATA, &ack, 1200u, NULL);

  AI_QUIC_ASSERT_EQ(conn->loss_state[AI_QUIC_PN_SPACE_APP_DATA].pto_count, 0u);
  AI_QUIC_ASSERT_EQ(conn->packet_spaces[AI_QUIC_PN_SPACE_APP_DATA].bytes_in_flight, 0u);
  AI_QUIC_ASSERT_EQ(conn->loss_state[AI_QUIC_PN_SPACE_APP_DATA].last_ack_ms, 1200u);

  ai_quic_conn_destroy((ai_quic_conn_t *)conn);
  return 0;
}

static int test_stream_before_1rtt_rejected(void) {
  ai_quic_conn_impl_t *conn;
  ai_quic_packet_t packet;
  ai_quic_pending_datagram_t pending[4];
  size_t pending_count;

  conn = (ai_quic_conn_impl_t *)ai_quic_conn_create(AI_QUIC_VERSION_V1, 1);
  AI_QUIC_ASSERT(conn != NULL);
  AI_QUIC_ASSERT(ai_quic_conn_init_transport(conn,
                                             ai_quic_tls_ctx_create(NULL),
                                             NULL,
                                             "hq-interop",
                                             NULL,
                                             AI_QUIC_TLS_CIPHER_POLICY_DEFAULT) ==
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
  AI_QUIC_ASSERT(test_transport_params_reject_malformed_version_information() == 0);
  AI_QUIC_ASSERT(test_version_catalog_v2_retry_and_reserved() == 0);
  AI_QUIC_ASSERT(test_version_information_error_code_split() == 0);
  AI_QUIC_ASSERT(test_tls_cipher_policy_chacha20_fake_secret() == 0);
  AI_QUIC_ASSERT(test_v2_long_header_roundtrip() == 0);
  AI_QUIC_ASSERT(test_flow_control_frames_roundtrip() == 0);
  AI_QUIC_ASSERT(test_stream_reassembly_out_of_order() == 0);
  AI_QUIC_ASSERT(test_stream_overlap_duplicate_and_final_size() == 0);
  AI_QUIC_ASSERT(test_stream_flow_control_updates_on_consume() == 0);
  AI_QUIC_ASSERT(test_stream_flow_control_updates_on_receive() == 0);
  AI_QUIC_ASSERT(test_ack_gap_preserves_bytes_in_flight() == 0);
  AI_QUIC_ASSERT(test_packet_threshold_marks_only_eligible_loss() == 0);
  AI_QUIC_ASSERT(test_lost_ranges_cleared_by_retransmit_ack() == 0);
  AI_QUIC_ASSERT(test_lost_range_retransmit_prefers_earliest_gap() == 0);
  AI_QUIC_ASSERT(test_pto_backoff_resets_on_new_ack() == 0);
  AI_QUIC_ASSERT(test_stream_before_1rtt_rejected() == 0);
  AI_QUIC_ASSERT(test_ack_range_generation() == 0);
  puts("ai_quic_unit_test: ok");
  return 0;
}
