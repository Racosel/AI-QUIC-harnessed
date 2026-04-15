#include "demo_internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ai_quic/fs.h"
#include "common_internal.h"

static int ai_quic_demo_pump(ai_quic_endpoint_t *client,
                             ai_quic_endpoint_t *server) {
  uint8_t buffer[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  ai_quic_conn_info_t info;
  size_t rounds;

  for (rounds = 0; rounds < 64u; ++rounds) {
    while (ai_quic_endpoint_has_pending_datagrams(client)) {
      if (ai_quic_endpoint_pop_datagram(client, buffer, sizeof(buffer), &written) !=
              AI_QUIC_OK ||
          ai_quic_endpoint_receive_datagram(server, buffer, written, ai_quic_now_ms()) !=
              AI_QUIC_OK) {
        return 1;
      }
    }
    while (ai_quic_endpoint_has_pending_datagrams(server)) {
      if (ai_quic_endpoint_pop_datagram(server, buffer, sizeof(buffer), &written) !=
              AI_QUIC_OK ||
          ai_quic_endpoint_receive_datagram(client, buffer, written, ai_quic_now_ms()) !=
              AI_QUIC_OK) {
        return 1;
      }
    }
    if (ai_quic_endpoint_connection_info(client, &info) == AI_QUIC_OK &&
        info.handshake_confirmed && info.state == AI_QUIC_CONN_STATE_ACTIVE &&
        !ai_quic_endpoint_has_pending_datagrams(client) &&
        !ai_quic_endpoint_has_pending_datagrams(server)) {
      return 0;
    }
  }
  return 1;
}

int ai_quic_demo_run_self_check(const ai_quic_demo_options_t *options) {
  ai_quic_endpoint_config_t client_config;
  ai_quic_endpoint_config_t server_config;
  ai_quic_endpoint_t *client;
  ai_quic_endpoint_t *server;
  static const uint8_t kBody[] = "hello from ai-quic";
  char request_url[256];
  char downloads_file[512];
  char qlog_client[512];
  char qlog_server[512];

  snprintf(qlog_client,
           sizeof(qlog_client),
           "%s/client.qlog",
           options->qlog_path != NULL ? options->qlog_path : "/tmp");
  snprintf(qlog_server,
           sizeof(qlog_server),
           "%s/server.qlog",
           options->qlog_path != NULL ? options->qlog_path : "/tmp");
  snprintf(downloads_file,
           sizeof(downloads_file),
           "%s/test.txt",
           options->downloads_dir != NULL ? options->downloads_dir : "/tmp");

  ai_quic_fs_ensure_dir(options->www_dir != NULL ? options->www_dir : "/tmp");
  ai_quic_fs_ensure_dir(options->downloads_dir != NULL ? options->downloads_dir : "/tmp");
  {
    char www_path[512];
    snprintf(www_path,
             sizeof(www_path),
             "%s/test.txt",
             options->www_dir != NULL ? options->www_dir : "/tmp");
    ai_quic_fs_write_binary_file(www_path, kBody, sizeof(kBody) - 1u);
  }

  ai_quic_endpoint_config_init(&client_config, AI_QUIC_ENDPOINT_ROLE_CLIENT);
  ai_quic_endpoint_config_init(&server_config, AI_QUIC_ENDPOINT_ROLE_SERVER);
  client_config.downloads_root = options->downloads_dir != NULL ? options->downloads_dir : "/tmp";
  server_config.www_root = options->www_dir != NULL ? options->www_dir : "/tmp";
  client_config.qlog_path = qlog_client;
  server_config.qlog_path = qlog_server;
  client_config.keylog_path = options->ssl_keylog_file;
  server_config.keylog_path = options->ssl_keylog_file;

  client = ai_quic_endpoint_create(&client_config);
  server = ai_quic_endpoint_create(&server_config);
  if (client == NULL || server == NULL) {
    ai_quic_endpoint_destroy(client);
    ai_quic_endpoint_destroy(server);
    return 1;
  }

  snprintf(request_url, sizeof(request_url), "%s", "server4:443");
  if (ai_quic_endpoint_start_client(client, request_url, "/test.txt") != AI_QUIC_OK ||
      ai_quic_demo_pump(client, server) != 0) {
    ai_quic_endpoint_destroy(client);
    ai_quic_endpoint_destroy(server);
    return 1;
  }

  if (options->log_file != NULL) {
    ai_quic_fs_write_text_file(options->log_file, "self-check ok\n");
  }
  {
    uint8_t *download_data;
    size_t download_len;
    if (ai_quic_fs_read_binary_file(downloads_file, &download_data, &download_len) !=
            AI_QUIC_OK ||
        download_len != sizeof(kBody) - 1u ||
        memcmp(download_data, kBody, download_len) != 0) {
      ai_quic_endpoint_destroy(client);
      ai_quic_endpoint_destroy(server);
      free(download_data);
      return 1;
    }
    free(download_data);
  }

  ai_quic_endpoint_destroy(client);
  ai_quic_endpoint_destroy(server);
  return 0;
}
