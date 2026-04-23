#include "test_support.h"

#include <stdlib.h>
#include <string.h>

#include "ai_quic/fs.h"
#include "ai_quic/log.h"
#include "transport_internal.h"

static int test_handshake_and_download(void) {
  ai_quic_endpoint_config_t client_config;
  ai_quic_endpoint_config_t server_config;
  ai_quic_endpoint_t *client;
  ai_quic_endpoint_t *server;
  ai_quic_fake_link_t link;
  static const uint8_t kPayload[] = "integration payload";
  char server_root[] = "/tmp/ai_quic_it_serverXXXXXX";
  char client_root[] = "/tmp/ai_quic_it_clientXXXXXX";
  char server_file[512];
  char client_file[512];
  uint8_t datagram[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  ai_quic_conn_info_t info;
  uint8_t *download_data;
  size_t download_len;

  AI_QUIC_ASSERT(mkdtemp(server_root) != NULL);
  AI_QUIC_ASSERT(mkdtemp(client_root) != NULL);
  snprintf(server_file, sizeof(server_file), "%s/%s", server_root, "test.txt");
  snprintf(client_file, sizeof(client_file), "%s/%s", client_root, "test.txt");
  AI_QUIC_ASSERT(ai_quic_fs_write_binary_file(server_file, kPayload, sizeof(kPayload) - 1u) ==
                 AI_QUIC_OK);

  ai_quic_endpoint_config_init(&client_config, AI_QUIC_ENDPOINT_ROLE_CLIENT);
  ai_quic_endpoint_config_init(&server_config, AI_QUIC_ENDPOINT_ROLE_SERVER);
  client_config.downloads_root = client_root;
  server_config.www_root = server_root;
  client_config.qlog_path = "/tmp/ai_quic_it_client.qlog";
  server_config.qlog_path = "/tmp/ai_quic_it_server.qlog";

  client = ai_quic_endpoint_create(&client_config);
  server = ai_quic_endpoint_create(&server_config);
  AI_QUIC_ASSERT(client != NULL);
  AI_QUIC_ASSERT(server != NULL);
  AI_QUIC_ASSERT(ai_quic_endpoint_start_client(client, "server4:443", "/test.txt") ==
                 AI_QUIC_OK);

  AI_QUIC_ASSERT(ai_quic_endpoint_pop_datagram(client, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  if (ai_quic_endpoint_receive_datagram(server, datagram, written, 1u) != AI_QUIC_OK) {
    fprintf(stderr, "server receive error: %s\n", ai_quic_endpoint_error(server));
    return 1;
  }
  AI_QUIC_ASSERT(ai_quic_endpoint_pop_datagram(server, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  if (ai_quic_endpoint_receive_datagram(client, datagram, written, 2u) != AI_QUIC_OK) {
    fprintf(stderr, "client receive error: %s\n", ai_quic_endpoint_error(client));
    return 1;
  }

  AI_QUIC_ASSERT(ai_quic_fake_link_init(&link, client, server) == 0);
  AI_QUIC_ASSERT(ai_quic_fake_link_pump(&link) == 0);
  AI_QUIC_ASSERT(ai_quic_endpoint_connection_info(client, &info) == AI_QUIC_OK);
  AI_QUIC_ASSERT(info.handshake_completed);
  AI_QUIC_ASSERT(info.handshake_confirmed);
  AI_QUIC_ASSERT(info.can_send_1rtt);
  AI_QUIC_ASSERT(info.state == AI_QUIC_CONN_STATE_ACTIVE);
  AI_QUIC_ASSERT(ai_quic_fs_read_binary_file(client_file, &download_data, &download_len) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(download_len == sizeof(kPayload) - 1u);
  AI_QUIC_ASSERT(memcmp(download_data, kPayload, download_len) == 0);
  free(download_data);
  ai_quic_endpoint_destroy(client);
  ai_quic_endpoint_destroy(server);
  return 0;
}

static void fill_pattern(uint8_t *buffer, size_t len, uint8_t seed) {
  size_t i;

  for (i = 0; i < len; ++i) {
    buffer[i] = (uint8_t)(seed + (i % 251u));
  }
}

static int test_multi_stream_transfer(void) {
  ai_quic_endpoint_config_t client_config;
  ai_quic_endpoint_config_t server_config;
  ai_quic_endpoint_t *client;
  ai_quic_endpoint_t *server;
  ai_quic_fake_link_t link;
  char server_root[] = "/tmp/ai_quic_transfer_serverXXXXXX";
  char client_root[] = "/tmp/ai_quic_transfer_clientXXXXXX";
  static const char *kNames[] = {"a.bin", "b.bin", "c.bin"};
  static const size_t kSizes[] = {400000u, 380000u, 5u * 1024u * 1024u};
  uint8_t *payloads[3];
  size_t i;
  ai_quic_conn_info_t info;

  AI_QUIC_ASSERT(mkdtemp(server_root) != NULL);
  AI_QUIC_ASSERT(mkdtemp(client_root) != NULL);

  for (i = 0; i < 3u; ++i) {
    char path[512];
    payloads[i] = (uint8_t *)malloc(kSizes[i]);
    AI_QUIC_ASSERT(payloads[i] != NULL);
    fill_pattern(payloads[i], kSizes[i], (uint8_t)(17u + i));
    snprintf(path, sizeof(path), "%s/%s", server_root, kNames[i]);
    AI_QUIC_ASSERT(ai_quic_fs_write_binary_file(path, payloads[i], kSizes[i]) == AI_QUIC_OK);
  }

  ai_quic_endpoint_config_init(&client_config, AI_QUIC_ENDPOINT_ROLE_CLIENT);
  ai_quic_endpoint_config_init(&server_config, AI_QUIC_ENDPOINT_ROLE_SERVER);
  client_config.downloads_root = client_root;
  server_config.www_root = server_root;
  client_config.qlog_path = "/tmp/ai_quic_transfer_client.qlog";
  server_config.qlog_path = "/tmp/ai_quic_transfer_server.qlog";

  client = ai_quic_endpoint_create(&client_config);
  server = ai_quic_endpoint_create(&server_config);
  AI_QUIC_ASSERT(client != NULL);
  AI_QUIC_ASSERT(server != NULL);
  AI_QUIC_ASSERT(ai_quic_endpoint_start_client(client, "server4:443", "/a.bin") == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_endpoint_queue_request(client, "/b.bin") == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_endpoint_queue_request(client, "/c.bin") == AI_QUIC_OK);

  AI_QUIC_ASSERT(ai_quic_fake_link_init(&link, client, server) == 0);
  AI_QUIC_ASSERT(ai_quic_fake_link_pump(&link) == 0);
  AI_QUIC_ASSERT(ai_quic_endpoint_connection_info(client, &info) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(info.total_request_streams, 3u);
  AI_QUIC_ASSERT_EQ(info.completed_request_streams, 3u);

  for (i = 0; i < 3u; ++i) {
    char client_file[512];
    uint8_t *download_data;
    size_t download_len;

    snprintf(client_file, sizeof(client_file), "%s/%s", client_root, kNames[i]);
    AI_QUIC_ASSERT(ai_quic_fs_read_binary_file(client_file, &download_data, &download_len) ==
                   AI_QUIC_OK);
    AI_QUIC_ASSERT_EQ(download_len, kSizes[i]);
    AI_QUIC_ASSERT(memcmp(download_data, payloads[i], download_len) == 0);
    free(download_data);
    free(payloads[i]);
  }

  ai_quic_endpoint_destroy(client);
  ai_quic_endpoint_destroy(server);
  return 0;
}

static int test_v2_handshake_and_download(void) {
  ai_quic_endpoint_config_t client_config;
  ai_quic_endpoint_config_t server_config;
  ai_quic_endpoint_t *client;
  ai_quic_endpoint_t *server;
  ai_quic_fake_link_t link;
  static const uint8_t kPayload[] = "v2 integration payload";
  char server_root[] = "/tmp/ai_quic_v2_serverXXXXXX";
  char client_root[] = "/tmp/ai_quic_v2_clientXXXXXX";
  char server_file[512];
  char client_file[512];
  uint8_t *download_data;
  size_t download_len;
  ai_quic_conn_info_t info;

  AI_QUIC_ASSERT(mkdtemp(server_root) != NULL);
  AI_QUIC_ASSERT(mkdtemp(client_root) != NULL);
  snprintf(server_file, sizeof(server_file), "%s/%s", server_root, "v2.txt");
  snprintf(client_file, sizeof(client_file), "%s/%s", client_root, "v2.txt");
  AI_QUIC_ASSERT(ai_quic_fs_write_binary_file(server_file, kPayload, sizeof(kPayload) - 1u) ==
                 AI_QUIC_OK);

  ai_quic_endpoint_config_init(&client_config, AI_QUIC_ENDPOINT_ROLE_CLIENT);
  ai_quic_endpoint_config_init(&server_config, AI_QUIC_ENDPOINT_ROLE_SERVER);
  client_config.downloads_root = client_root;
  server_config.www_root = server_root;
  client_config.qlog_path = "/tmp/ai_quic_v2_client.qlog";
  server_config.qlog_path = "/tmp/ai_quic_v2_server.qlog";
  client_config.testcase = "v2";
  server_config.testcase = "v2";

  client = ai_quic_endpoint_create(&client_config);
  server = ai_quic_endpoint_create(&server_config);
  AI_QUIC_ASSERT(client != NULL);
  AI_QUIC_ASSERT(server != NULL);
  AI_QUIC_ASSERT(ai_quic_endpoint_start_client(client, "server4:443", "/v2.txt") ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_fake_link_init(&link, client, server) == 0);
  AI_QUIC_ASSERT(ai_quic_fake_link_pump(&link) == 0);
  AI_QUIC_ASSERT(ai_quic_endpoint_connection_info(client, &info) == AI_QUIC_OK);
  AI_QUIC_ASSERT_EQ(info.original_version, AI_QUIC_VERSION_V1);
  AI_QUIC_ASSERT_EQ(info.negotiated_version, AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT_EQ(info.version, AI_QUIC_VERSION_V2);
  AI_QUIC_ASSERT(info.handshake_completed);
  AI_QUIC_ASSERT(info.handshake_confirmed);
  AI_QUIC_ASSERT(info.can_send_1rtt);
  AI_QUIC_ASSERT(ai_quic_fs_read_binary_file(client_file, &download_data, &download_len) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(download_len == sizeof(kPayload) - 1u);
  AI_QUIC_ASSERT(memcmp(download_data, kPayload, download_len) == 0);
  free(download_data);
  ai_quic_endpoint_destroy(client);
  ai_quic_endpoint_destroy(server);
  return 0;
}

static int test_chacha20_handshake_and_download(void) {
  ai_quic_endpoint_config_t client_config;
  ai_quic_endpoint_config_t server_config;
  ai_quic_endpoint_t *client;
  ai_quic_endpoint_t *server;
  ai_quic_fake_link_t link;
  static const uint8_t kPayload[] = "chacha20 integration payload";
  char server_root[] = "/tmp/ai_quic_chacha_serverXXXXXX";
  char client_root[] = "/tmp/ai_quic_chacha_clientXXXXXX";
  char server_file[512];
  char client_file[512];
  uint8_t *download_data;
  size_t download_len;
  ai_quic_conn_info_t info;

  AI_QUIC_ASSERT(mkdtemp(server_root) != NULL);
  AI_QUIC_ASSERT(mkdtemp(client_root) != NULL);
  snprintf(server_file, sizeof(server_file), "%s/%s", server_root, "chacha.txt");
  snprintf(client_file, sizeof(client_file), "%s/%s", client_root, "chacha.txt");
  AI_QUIC_ASSERT(ai_quic_fs_write_binary_file(server_file, kPayload, sizeof(kPayload) - 1u) ==
                 AI_QUIC_OK);

  ai_quic_endpoint_config_init(&client_config, AI_QUIC_ENDPOINT_ROLE_CLIENT);
  ai_quic_endpoint_config_init(&server_config, AI_QUIC_ENDPOINT_ROLE_SERVER);
  client_config.downloads_root = client_root;
  server_config.www_root = server_root;
  client_config.qlog_path = "/tmp/ai_quic_chacha_client.qlog";
  server_config.qlog_path = "/tmp/ai_quic_chacha_server.qlog";
  client_config.cipher_policy = AI_QUIC_TLS_CIPHER_POLICY_CHACHA20_ONLY;
  server_config.cipher_policy = AI_QUIC_TLS_CIPHER_POLICY_CHACHA20_ONLY;

  client = ai_quic_endpoint_create(&client_config);
  server = ai_quic_endpoint_create(&server_config);
  AI_QUIC_ASSERT(client != NULL);
  AI_QUIC_ASSERT(server != NULL);
  AI_QUIC_ASSERT(ai_quic_endpoint_start_client(client, "server4:443", "/chacha.txt") ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_fake_link_init(&link, client, server) == 0);
  AI_QUIC_ASSERT(ai_quic_fake_link_pump(&link) == 0);
  AI_QUIC_ASSERT(ai_quic_endpoint_connection_info(client, &info) == AI_QUIC_OK);
  AI_QUIC_ASSERT(info.handshake_completed);
  AI_QUIC_ASSERT(info.handshake_confirmed);
  AI_QUIC_ASSERT(info.can_send_1rtt);
  AI_QUIC_ASSERT(ai_quic_fs_read_binary_file(client_file, &download_data, &download_len) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(download_len == sizeof(kPayload) - 1u);
  AI_QUIC_ASSERT(memcmp(download_data, kPayload, download_len) == 0);
  free(download_data);
  ai_quic_endpoint_destroy(client);
  ai_quic_endpoint_destroy(server);
  return 0;
}

int main(void) {
  ai_quic_log_set_level(AI_QUIC_LOG_ERROR);
  AI_QUIC_ASSERT(test_handshake_and_download() == 0);
  AI_QUIC_ASSERT(test_multi_stream_transfer() == 0);
  AI_QUIC_ASSERT(test_v2_handshake_and_download() == 0);
  AI_QUIC_ASSERT(test_chacha20_handshake_and_download() == 0);
  puts("ai_quic_integration_test: ok");
  return 0;
}
