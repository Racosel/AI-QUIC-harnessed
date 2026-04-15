#include "test_support.h"

#include <string.h>

#include "ai_quic/dispatcher.h"
#include "transport_internal.h"

int main(void) {
  ai_quic_endpoint_config_t server_config;
  ai_quic_endpoint_t *server;
  ai_quic_packet_t initial;
  ai_quic_packet_t vn;
  uint8_t datagram[AI_QUIC_MAX_PACKET_SIZE];
  size_t written;
  size_t consumed;

  ai_quic_endpoint_config_init(&server_config, AI_QUIC_ENDPOINT_ROLE_SERVER);
  server_config.qlog_path = "/tmp/ai_quic_vn_test.qlog";
  server = ai_quic_endpoint_create(&server_config);
  AI_QUIC_ASSERT(server != NULL);

  memset(&initial, 0, sizeof(initial));
  initial.header.type = AI_QUIC_PACKET_TYPE_INITIAL;
  initial.header.version = 0xfaceb00cu;
  AI_QUIC_ASSERT(ai_quic_random_cid(&initial.header.dcid, 8u) == AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_random_cid(&initial.header.scid, 8u) == AI_QUIC_OK);
  initial.frames[0].type = AI_QUIC_FRAME_PADDING;
  initial.frame_count = 1u;
  AI_QUIC_ASSERT(ai_quic_packet_encode(&initial, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  while (written < AI_QUIC_MIN_INITIAL_DATAGRAM_SIZE) {
    datagram[written++] = 0u;
  }

  AI_QUIC_ASSERT(ai_quic_endpoint_receive_datagram(server, datagram, written, 1u) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_endpoint_pop_datagram(server, datagram, sizeof(datagram), &written) ==
                 AI_QUIC_OK);
  AI_QUIC_ASSERT(ai_quic_packet_decode(datagram, written, &consumed, &vn) == AI_QUIC_OK);
  AI_QUIC_ASSERT(vn.header.type == AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION);
  AI_QUIC_ASSERT(vn.supported_versions_len == 1u);
  AI_QUIC_ASSERT(vn.supported_versions[0] == AI_QUIC_VERSION_V1);
  ai_quic_endpoint_destroy(server);
  puts("ai_quic_interop_vn_test: ok");
  return 0;
}
