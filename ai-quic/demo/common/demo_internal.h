#ifndef AI_QUIC_DEMO_INTERNAL_H
#define AI_QUIC_DEMO_INTERNAL_H

#include <stdint.h>

#include "ai_quic/endpoint.h"

typedef struct ai_quic_demo_options {
  const char *role;
  const char *log_file;
  const char *qlog_path;
  const char *ssl_keylog_file;
  const char *cert_dir;
  const char *www_dir;
  const char *downloads_dir;
  const char *requests;
  const char *testcase;
  const char *bind_host;
  uint16_t port;
  int run_mode;
  int self_check;
  int show_help;
} ai_quic_demo_options_t;

void ai_quic_demo_print_help(const char *program_name, const char *role);
int ai_quic_demo_parse_options(int argc,
                               char **argv,
                               const char *role,
                               ai_quic_demo_options_t *options);
int ai_quic_demo_run(const char *program_name,
                     const ai_quic_demo_options_t *options);
int ai_quic_demo_run_self_check(const ai_quic_demo_options_t *options);
int ai_quic_demo_run_server(const ai_quic_demo_options_t *options);
int ai_quic_demo_run_client(const ai_quic_demo_options_t *options);
ai_quic_result_t ai_quic_demo_build_endpoint_config(
    const ai_quic_demo_options_t *options,
    ai_quic_endpoint_role_t role,
    ai_quic_endpoint_config_t *config);

#endif
