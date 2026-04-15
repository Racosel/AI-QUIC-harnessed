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

typedef struct ai_quic_demo_bound_socket {
  int fd;
  int family;
  char label[160];
} ai_quic_demo_bound_socket_t;

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

static int ai_quic_demo_bind_one(ai_quic_demo_bound_socket_t *slot,
                                 const char *host,
                                 uint16_t port,
                                 int family,
                                 int v6only) {
  struct addrinfo hints;
  struct addrinfo *result;
  struct addrinfo *cursor;
  char port_text[16];
  int sock;
  int enable;
  char addr_text[128];

  if (slot == NULL) {
    return -1;
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;
  snprintf(port_text, sizeof(port_text), "%u", port);
  if (getaddrinfo(host, port_text, &hints, &result) != 0) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR,
                      "demo_server",
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

    enable = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (cursor->ai_family == AF_INET6) {
      (void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    if (bind(sock, cursor->ai_addr, cursor->ai_addrlen) == 0) {
      slot->fd = sock;
      slot->family = cursor->ai_family;
      ai_quic_demo_format_sockaddr(cursor->ai_addr,
                                   (socklen_t)cursor->ai_addrlen,
                                   addr_text,
                                   sizeof(addr_text));
      snprintf(slot->label,
               sizeof(slot->label),
               "%s %s",
               cursor->ai_family == AF_INET6 ? "udp6" : "udp4",
               addr_text);
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "demo_server",
                        "listening on %s",
                        slot->label);
      freeaddrinfo(result);
      return 0;
    }

    ai_quic_log_write(AI_QUIC_LOG_WARN,
                      "demo_server",
                      "bind failed family=%d host=%s port=%u errno=%d (%s)",
                      cursor->ai_family,
                      host != NULL ? host : "(null)",
                      (unsigned int)port,
                      errno,
                      strerror(errno));
    close(sock);
    sock = -1;
  }

  freeaddrinfo(result);
  return -1;
}

static size_t ai_quic_demo_bind_server_sockets(const ai_quic_demo_options_t *options,
                                               ai_quic_demo_bound_socket_t *sockets,
                                               size_t capacity) {
  size_t count;
  const char *host;

  if (options == NULL || sockets == NULL || capacity == 0u) {
    return 0u;
  }

  count = 0u;
  host = options->bind_host;

  if (host == NULL || strcmp(host, "::") == 0 || strcmp(host, "0.0.0.0") == 0) {
    if (count < capacity &&
        ai_quic_demo_bind_one(&sockets[count], "0.0.0.0", options->port, AF_INET, 0) == 0) {
      count += 1u;
    }
    if (count < capacity &&
        ai_quic_demo_bind_one(&sockets[count], "::", options->port, AF_INET6, 1) == 0) {
      count += 1u;
    }
    return count;
  }

  if (ai_quic_demo_bind_one(&sockets[count], host, options->port, AF_UNSPEC, 0) == 0) {
    count += 1u;
  }
  return count;
}

int ai_quic_demo_run_server(const ai_quic_demo_options_t *options) {
  ai_quic_endpoint_config_t config;
  ai_quic_endpoint_t *endpoint;
  uint8_t buffer[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  ai_quic_demo_bound_socket_t sockets[2];
  size_t socket_count;
  size_t i;
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len;
  ssize_t received;
  ai_quic_conn_info_t info;
  fd_set readfds;
  int max_fd;
  int ready;
  char peer_text[128];
  char preview[64];

  if (ai_quic_demo_build_endpoint_config(
          options, AI_QUIC_ENDPOINT_ROLE_SERVER, &config) != AI_QUIC_OK) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR, "demo_server", "build endpoint config failed");
    return 1;
  }

  ai_quic_log_write(AI_QUIC_LOG_INFO,
                    "demo_server",
                    "starting server testcase=%s bind_host=%s port=%u www=%s downloads=%s qlog=%s",
                    options->testcase != NULL ? options->testcase : "(unset)",
                    options->bind_host != NULL ? options->bind_host : "(unset)",
                    (unsigned int)options->port,
                    options->www_dir != NULL ? options->www_dir : "(unset)",
                    options->downloads_dir != NULL ? options->downloads_dir : "(unset)",
                    options->qlog_path != NULL ? options->qlog_path : "(unset)");

  endpoint = ai_quic_endpoint_create(&config);
  if (endpoint == NULL) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR, "demo_server", "endpoint create failed");
    return 1;
  }

  memset(sockets, 0, sizeof(sockets));
  for (i = 0; i < AI_QUIC_ARRAY_LEN(sockets); ++i) {
    sockets[i].fd = -1;
  }

  socket_count = ai_quic_demo_bind_server_sockets(options, sockets, AI_QUIC_ARRAY_LEN(sockets));
  if (socket_count == 0u) {
    ai_quic_log_write(AI_QUIC_LOG_ERROR,
                      "demo_server",
                      "no UDP socket could be bound for host=%s port=%u",
                      options->bind_host != NULL ? options->bind_host : "(unset)",
                      (unsigned int)options->port);
    ai_quic_endpoint_destroy(endpoint);
    return 1;
  }

  for (;;) {
    FD_ZERO(&readfds);
    max_fd = -1;
    for (i = 0; i < socket_count; ++i) {
      FD_SET(sockets[i].fd, &readfds);
      if (sockets[i].fd > max_fd) {
        max_fd = sockets[i].fd;
      }
    }

    ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      ai_quic_log_write(AI_QUIC_LOG_ERROR,
                        "demo_server",
                        "select failed errno=%d (%s)",
                        errno,
                        strerror(errno));
      break;
    }

    for (i = 0; i < socket_count; ++i) {
      if (!FD_ISSET(sockets[i].fd, &readfds)) {
        continue;
      }

      peer_addr_len = sizeof(peer_addr);
      received = recvfrom(sockets[i].fd,
                          buffer,
                          sizeof(buffer),
                          0,
                          (struct sockaddr *)&peer_addr,
                          &peer_addr_len);
      if (received <= 0) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "demo_server",
                          "recvfrom failed on %s errno=%d (%s)",
                          sockets[i].label,
                          errno,
                          strerror(errno));
        goto cleanup_error;
      }

      ai_quic_demo_format_sockaddr((const struct sockaddr *)&peer_addr,
                                   peer_addr_len,
                                   peer_text,
                                   sizeof(peer_text));
      ai_quic_demo_format_preview_hex(buffer, (size_t)received, preview, sizeof(preview));
      ai_quic_log_write(AI_QUIC_LOG_INFO,
                        "demo_server",
                        "received datagram len=%zd on %s from %s preview=%s",
                        received,
                        sockets[i].label,
                        peer_text,
                        preview);

      if (ai_quic_endpoint_receive_datagram(endpoint,
                                            buffer,
                                            (size_t)received,
                                            ai_quic_now_ms()) != AI_QUIC_OK) {
        ai_quic_log_write(AI_QUIC_LOG_ERROR,
                          "demo_server",
                          "endpoint receive failed: %s",
                          ai_quic_endpoint_error(endpoint));
        goto cleanup_error;
      }

      while (ai_quic_endpoint_has_pending_datagrams(endpoint)) {
        if (ai_quic_endpoint_pop_datagram(endpoint, buffer, sizeof(buffer), &written) !=
                AI_QUIC_OK ||
            sendto(sockets[i].fd,
                   buffer,
                   written,
                   0,
                   (struct sockaddr *)&peer_addr,
                   peer_addr_len) < 0) {
          ai_quic_log_write(AI_QUIC_LOG_ERROR,
                            "demo_server",
                            "sendto failed on %s to %s errno=%d (%s)",
                            sockets[i].label,
                            peer_text,
                            errno,
                            strerror(errno));
          goto cleanup_error;
        }
        ai_quic_demo_format_preview_hex(buffer, written, preview, sizeof(preview));
        ai_quic_log_write(AI_QUIC_LOG_INFO,
                          "demo_server",
                          "sent datagram len=%zu on %s to %s preview=%s",
                          written,
                          sockets[i].label,
                          peer_text,
                          preview);
      }

      if (ai_quic_endpoint_connection_info(endpoint, &info) == AI_QUIC_OK &&
          info.handshake_confirmed && info.can_send_1rtt &&
          info.state == AI_QUIC_CONN_STATE_ACTIVE) {
        ai_quic_log_write(AI_QUIC_LOG_INFO,
                          "demo_server",
                          "connection active and handshake confirmed; waiting for request/response completion");
      }
    }
  }

cleanup_error:
  for (i = 0; i < socket_count; ++i) {
    if (sockets[i].fd >= 0) {
      close(sockets[i].fd);
    }
  }
  ai_quic_endpoint_destroy(endpoint);
  return 1;
}
