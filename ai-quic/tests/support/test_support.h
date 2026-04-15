#ifndef AI_QUIC_TEST_SUPPORT_H
#define AI_QUIC_TEST_SUPPORT_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ai_quic/endpoint.h"

#define AI_QUIC_ASSERT(condition)                                                \
  do {                                                                           \
    if (!(condition)) {                                                          \
      fprintf(stderr,                                                             \
              "%s:%d assertion failed: %s\n",                                    \
              __FILE__,                                                           \
              __LINE__,                                                           \
              #condition);                                                       \
      return 1;                                                                  \
    }                                                                            \
  } while (0)

#define AI_QUIC_ASSERT_EQ(lhs, rhs) AI_QUIC_ASSERT((lhs) == (rhs))

typedef struct ai_quic_fake_link {
  ai_quic_endpoint_t *client;
  ai_quic_endpoint_t *server;
  uint64_t now_ms;
} ai_quic_fake_link_t;

int ai_quic_fake_link_init(ai_quic_fake_link_t *link,
                           ai_quic_endpoint_t *client,
                           ai_quic_endpoint_t *server);
int ai_quic_fake_link_pump(ai_quic_fake_link_t *link);
int ai_quic_write_fixture_file(const char *root,
                               const char *name,
                               const uint8_t *data,
                               size_t data_len);
int ai_quic_read_fixture_file(const char *path,
                              uint8_t *buffer,
                              size_t capacity,
                              size_t *read_len);

#endif
