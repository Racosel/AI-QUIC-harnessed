#include "common_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ai_quic_path_extract_filename(const char *path,
                                  char *buffer,
                                  size_t capacity) {
  const char *slash;
  const char *name;
  size_t len;

  if (path == NULL || buffer == NULL || capacity == 0u) {
    return 0;
  }

  slash = strrchr(path, '/');
  name = slash != NULL ? slash + 1 : path;
  if (name[0] == '\0') {
    name = "index";
  }
  len = strlen(name);
  if (len + 1u > capacity) {
    return 0;
  }
  memcpy(buffer, name, len + 1u);
  return 1;
}

int ai_quic_url_split(const char *url,
                      char *host,
                      size_t host_capacity,
                      uint16_t *port,
                      char *path,
                      size_t path_capacity) {
  const char *authority;
  const char *path_start;
  const char *colon;
  size_t authority_len;
  size_t host_len;
  long parsed_port;

  if (url == NULL || host == NULL || port == NULL || path == NULL) {
    return 0;
  }

  authority = strstr(url, "://");
  authority = authority != NULL ? authority + 3 : url;
  path_start = strchr(authority, '/');
  authority_len = path_start != NULL ? (size_t)(path_start - authority)
                                     : strlen(authority);

  colon = memchr(authority, ':', authority_len);
  if (colon != NULL) {
    host_len = (size_t)(colon - authority);
    parsed_port = strtol(colon + 1, NULL, 10);
    if (parsed_port <= 0 || parsed_port > 65535) {
      return 0;
    }
    *port = (uint16_t)parsed_port;
  } else {
    host_len = authority_len;
    *port = 443u;
  }

  if (host_len + 1u > host_capacity) {
    return 0;
  }

  memcpy(host, authority, host_len);
  host[host_len] = '\0';

  if (path_start == NULL) {
    if (path_capacity < 2u) {
      return 0;
    }
    memcpy(path, "/", 2u);
    return 1;
  }

  if (strlen(path_start) + 1u > path_capacity) {
    return 0;
  }

  memcpy(path, path_start, strlen(path_start) + 1u);
  return 1;
}
