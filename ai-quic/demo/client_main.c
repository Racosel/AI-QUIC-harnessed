#include "common/demo_internal.h"

int main(int argc, char **argv) {
  ai_quic_demo_options_t ai_quic_demo_options;

  if (ai_quic_demo_parse_options(
          argc, argv, "client", &ai_quic_demo_options) != 0) {
    return 1;
  }

  return ai_quic_demo_run("ai_quic_demo_client", &ai_quic_demo_options);
}
