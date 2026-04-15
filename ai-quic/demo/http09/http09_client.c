#include "demo_internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "ai_quic/log.h"
#include "common_internal.h"

static void ai_quic_demo_format_sockaddr(const struct sockaddr *addr,
                                         socklen_t addr_len,
                                         char *buffer,
                                         size_t capacity) {
  char host[128];
  char service[32];

  if (buffer == NULL || capacity == 0u) {
    return;
  }
  buffer[0] = '\0';

  if (addr == NULL) {
    snprintf(buffer, capacity, "(null)");
    return;
  }

  if (getnameinfo(addr,
                  addr_len,
                  host,
                  sizeof(host),
                  service,
                  sizeof(service),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    snprintf(buffer, capacity, "(unresolved)");
    return;
  }

  snprintf(buffer, capacity, "%s:%s", host, service);
}

static void ai_quic_demo_format_preview_hex(const uint8_t *bytes,
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

static int ai_quic_demo_send_pending_client(ai_quic_endpoint_t *endpoint,
                                            int sock,
                                            const struct sockaddr *peer_addr,
                                            socklen_t peer_addr_len,
                                            const char *peer_text) {
  uint8_t buffer[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  char preview[64];

  if (endpoint == NULL || peer_addr == NULL) {
    return -1;
  }

  while (ai_quic_endpoint_has_pending_datagrams(endpoint)) {
    if (ai_quic_endpoint_pop_datagram(endpoint, buffer, sizeof(buffer), &written) != AI_QUIC_OK) {
      return -1;
    }
    if (sendto(sock, buffer, written, 0, peer_addr, peer_addr_len) < 0) {
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "demo_client",
                        "sendto failed to %s errno=%d (%s)",
                        peer_text != NULL ? peer_text : "(unknown)",
                        errno,
                        strerror(errno));
      return -1;
    }
    ai_quic_demo_format_preview_hex(buffer, written, preview, sizeof(preview));
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "demo_client",
                      "sent datagram len=%zu to %s preview=%s",
                      written,
                      peer_text != NULL ? peer_text : "(unknown)",
                      preview);
  }

  return 0;
}

static int ai_quic_open_udp_socket(const char *host,
                                   uint16_t port,
                                   struct sockaddr_storage *peer_addr,
                                   socklen_t *peer_addr_len,
                                   char *peer_text,
                                   size_t peer_text_capacity) {
  struct addrinfo hints;
  struct addrinfo *result;
  struct addrinfo *cursor;
  char port_text[16];
  int sock;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  snprintf(port_text, sizeof(port_text), "%u", port);
  if (getaddrinfo(host, port_text, &hints, &result) != 0) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR,
                      "demo_client",
                      "getaddrinfo failed for host=%s port=%s",
                      host != NULL ? host : "(null)",
                      port_text);
    return -1;
  }

  sock = -1;
  for (cursor = result; cursor != NULL; cursor = cursor->ai_next) {
    sock = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
    if (sock < 0) {
      continue;
    }

    memcpy(peer_addr, cursor->ai_addr, cursor->ai_addrlen);
    *peer_addr_len = (socklen_t)cursor->ai_addrlen;
    ai_quic_demo_format_sockaddr(cursor->ai_addr,
                                 (socklen_t)cursor->ai_addrlen,
                                 peer_text,
                                 peer_text_capacity);
    ai_quic_log_write(AI_QUIC_LOG_INFO,
                      "demo_client",
                      "opened UDP socket family=%d peer=%s",
                      cursor->ai_family,
                      peer_text);
    freeaddrinfo(result);
    return sock;
  }

  freeaddrinfo(result);
  ai_quic_log_write(AI_QUIC_LOG_ERROR,
                    "demo_client",
                    "socket creation failed for host=%s port=%u",
                    host != NULL ? host : "(null)",
                    (unsigned int)port);
  return -1;
}

static int ai_quic_demo_queue_requests(ai_quic_endpoint_t *endpoint,
                                       const ai_quic_demo_options_t *options,
                                       char *requests_copy,
                                       char *host,
                                       size_t host_capacity,
                                       uint16_t *port_out) {
  char *cursor;
  char *token;
  int saw_request;

  saw_request = 0;
  cursor = requests_copy;
  while ((token = strtok_r(cursor, " ", &cursor)) != NULL) {
    char token_host[256];
    char token_path[512];
    uint16_t token_port;

    if (!ai_quic_url_split(token,
                           token_host,
                           sizeof(token_host),
                           &token_port,
                           token_path,
                           sizeof(token_path))) {
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "demo_client",
                        "invalid request string: %s",
                        token);
      return 1;
    }

    if (!saw_request) {
      snprintf(host, host_capacity, "%s", token_host);
      *port_out = token_port;
      if (ai_quic_endpoint_start_client(endpoint, token_host, token_path) != AI_QUIC_OK) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "demo_client",
                          "endpoint start failed: %s",
                          ai_quic_endpoint_error(endpoint));
        return 1;
      }
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "demo_client",
                        "starting client testcase=%s first_url=%s host=%s port=%u downloads=%s qlog=%s",
                        options->testcase != NULL ? options->testcase : "(unset)",
                        token,
                        token_host,
                        (unsigned int)token_port,
                        options->downloads_dir != NULL ? options->downloads_dir : "(unset)",
                        options->qlog_path != NULL ? options->qlog_path : "(unset)");
      saw_request = 1;
      continue;
    }

    if (strcmp(host, token_host) != 0 || *port_out != token_port) {
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "demo_client",
                        "all transfer requests must share the same authority: first=%s:%u current=%s:%u",
                        host,
                        (unsigned int)*port_out,
                        token_host,
                        (unsigned int)token_port);
      return 1;
    }
    if (ai_quic_endpoint_queue_request(endpoint, token_path) != AI_QUIC_OK) {
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "demo_client",
                        "queue request failed for %s: %s",
                        token_path,
                        ai_quic_endpoint_error(endpoint));
      return 1;
    }
  }

  if (!saw_request) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR,
                      "demo_client",
                      "no request URL found in requests string: %s",
                      options->requests != NULL ? options->requests : "(unset)");
    return 1;
  }

  return 0;
}

int ai_quic_demo_run_client(const ai_quic_demo_options_t *options) {
  ai_quic_endpoint_config_t config;
  ai_quic_endpoint_t *endpoint;
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len;
  char peer_text[128];
  char host[256];
  uint16_t port;
  uint8_t buffer[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  int sock;
  char *requests_copy;
  ai_quic_conn_info_t info;
  ssize_t received;
  char preview[64];
  fd_set readfds;
  struct timeval timeout;
  int ready;

  if (options == NULL || options->requests == NULL) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR,
                      "demo_client",
                      "invalid requests string: %s",
                      options != NULL && options->requests != NULL ? options->requests
                                                                   : "(unset)");
    return 1;
  }

  if (ai_quic_demo_build_endpoint_config(
          options, AI_QUIC_ENDPOINT_ROLE_CLIENT, &config) != AI_QUIC_OK) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR, "demo_client", "build endpoint config failed");
    return 1;
  }

  endpoint = ai_quic_endpoint_create(&config);
  if (endpoint == NULL) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR, "demo_client", "endpoint create failed");
    return 1;
  }

  requests_copy = strdup(options->requests);
  if (requests_copy == NULL) {
    ai_quic_endpoint_destroy(endpoint);
    ai_quic_log_write(AI_QUIC_LOG_ERROR, "demo_client", "strdup requests failed");
    return 1;
  }

  if (ai_quic_demo_queue_requests(endpoint,
                                  options,
                                  requests_copy,
                                  host,
                                  sizeof(host),
                                  &port) != 0) {
    free(requests_copy);
    ai_quic_endpoint_destroy(endpoint);
    return 1;
  }
  free(requests_copy);

  sock = ai_quic_open_udp_socket(host,
                                 port,
                                 &peer_addr,
                                 &peer_addr_len,
                                 peer_text,
                                 sizeof(peer_text));
  if (sock < 0) {
    ai_quic_endpoint_destroy(endpoint);
    return 1;
  }

  if (ai_quic_demo_send_pending_client(endpoint,
                                       sock,
                                       (const struct sockaddr *)&peer_addr,
                                       peer_addr_len,
                                       peer_text) != 0) {
    close(sock);
    ai_quic_endpoint_destroy(endpoint);
    return 1;
  }

  for (;;) {
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;
    ready = select(sock + 1, &readfds, NULL, NULL, &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "demo_client",
                        "select failed errno=%d (%s)",
                        errno,
                        strerror(errno));
      close(sock);
      ai_quic_endpoint_destroy(endpoint);
      return 1;
    }
    if (ready == 0) {
      if (ai_quic_endpoint_on_timeout(endpoint, ai_quic_now_ms()) != AI_QUIC_OK) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "demo_client",
                          "endpoint timeout failed: %s",
                          ai_quic_endpoint_error(endpoint));
        close(sock);
        ai_quic_endpoint_destroy(endpoint);
        return 1;
      }
      if (ai_quic_demo_send_pending_client(endpoint,
                                           sock,
                                           (const struct sockaddr *)&peer_addr,
                                           peer_addr_len,
                                           peer_text) != 0) {
        close(sock);
        ai_quic_endpoint_destroy(endpoint);
        return 1;
      }
    } else {
      received = recvfrom(sock, buffer, sizeof(buffer), 0, NULL, NULL);
      if (received <= 0) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "demo_client",
                          "recvfrom failed errno=%d (%s)",
                          errno,
                          strerror(errno));
        close(sock);
        ai_quic_endpoint_destroy(endpoint);
        return 1;
      }

      ai_quic_demo_format_preview_hex(buffer, (size_t)received, preview, sizeof(preview));
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "demo_client",
                        "received datagram len=%zd from %s preview=%s",
                        received,
                        peer_text,
                        preview);

      if (ai_quic_endpoint_receive_datagram(endpoint,
                                            buffer,
                                            (size_t)received,
                                            ai_quic_now_ms()) != AI_QUIC_OK) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "demo_client",
                          "endpoint receive failed: %s",
                          ai_quic_endpoint_error(endpoint));
        close(sock);
        ai_quic_endpoint_destroy(endpoint);
        return 1;
      }
      if (ai_quic_demo_send_pending_client(endpoint,
                                           sock,
                                           (const struct sockaddr *)&peer_addr,
                                           peer_addr_len,
                                           peer_text) != 0) {
        close(sock);
        ai_quic_endpoint_destroy(endpoint);
        return 1;
      }
    }

    if (ai_quic_endpoint_connection_info(endpoint, &info) == AI_QUIC_OK &&
        info.handshake_confirmed && info.state == AI_QUIC_CONN_STATE_ACTIVE &&
        info.total_request_streams > 0u &&
        info.completed_request_streams == info.total_request_streams) {
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "demo_client",
                        "all request streams completed: %zu/%zu",
                        info.completed_request_streams,
                        info.total_request_streams);
      close(sock);
      ai_quic_endpoint_destroy(endpoint);
      return 0;
    }
  }
}
