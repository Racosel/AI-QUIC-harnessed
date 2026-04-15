#include "demo_internal.h"

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ai_quic/log.h"

void ai_quic_demo_print_help(const char *program_name, const char *role) {
  printf("%s (%s)\n", program_name, role);
  printf("Usage:\n");
  printf("  %s --help\n", program_name);
  printf("  %s --self-check [options]\n", program_name);
  printf("  %s [options]\n", program_name);
  printf("\nOptions:\n");
  printf("  --log-file PATH\n");
  printf("  --qlog-dir PATH\n");
  printf("  --ssl-keylog-file PATH\n");
  printf("  --cert-dir PATH\n");
  printf("  --www PATH\n");
  printf("  --downloads PATH\n");
  printf("  --requests STRING\n");
  printf("  --testcase NAME\n");
  printf("  --bind-host HOST\n");
  printf("  --port N\n");
}

int ai_quic_demo_parse_options(int argc,
                               char **argv,
                               const char *role,
                               ai_quic_demo_options_t *options) {
  int opt;
  int option_index;
  static struct option long_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"self-check", no_argument, NULL, 's'},
      {"run", no_argument, NULL, 'r'},
      {"log-file", required_argument, NULL, 'l'},
      {"qlog-dir", required_argument, NULL, 'q'},
      {"ssl-keylog-file", required_argument, NULL, 'k'},
      {"cert-dir", required_argument, NULL, 'c'},
      {"www", required_argument, NULL, 'w'},
      {"downloads", required_argument, NULL, 'd'},
      {"requests", required_argument, NULL, 'R'},
      {"testcase", required_argument, NULL, 't'},
      {"bind-host", required_argument, NULL, 'b'},
      {"port", required_argument, NULL, 'p'},
      {NULL, 0, NULL, 0}};

  memset(options, 0, sizeof(*options));
  options->role = role;
  options->bind_host = "0.0.0.0";
  options->port = 443u;

  optind = 1;
  while ((opt = getopt_long(argc,
                            argv,
                            "hsrl:q:k:c:w:d:R:t:b:p:",
                            long_options,
                            &option_index)) != -1) {
    (void)option_index;
    switch (opt) {
      case 'h':
        options->show_help = 1;
        break;
      case 's':
        options->self_check = 1;
        break;
      case 'r':
        options->run_mode = 1;
        break;
      case 'l':
        options->log_file = optarg;
        break;
      case 'q':
        options->qlog_path = optarg;
        break;
      case 'k':
        options->ssl_keylog_file = optarg;
        break;
      case 'c':
        options->cert_dir = optarg;
        break;
      case 'w':
        options->www_dir = optarg;
        break;
      case 'd':
        options->downloads_dir = optarg;
        break;
      case 'R':
        options->requests = optarg;
        break;
      case 't':
        options->testcase = optarg;
        break;
      case 'b':
        options->bind_host = optarg;
        break;
      case 'p':
        options->port = (uint16_t)atoi(optarg);
        break;
      default:
        return 1;
    }
  }

  return 0;
}

ai_quic_result_t ai_quic_demo_build_endpoint_config(
    const ai_quic_demo_options_t *options,
    ai_quic_endpoint_role_t role,
    ai_quic_endpoint_config_t *config) {
  if (options == NULL || config == NULL) {
    return AI_QUIC_ERROR;
  }

  ai_quic_endpoint_config_init(config, role);
  config->log_path = options->log_file;
  config->qlog_path = options->qlog_path;
  config->keylog_path = options->ssl_keylog_file;
  config->cert_root = options->cert_dir;
  config->www_root = options->www_dir;
  config->downloads_root = options->downloads_dir;
  return AI_QUIC_OK;
}

int ai_quic_demo_run(const char *program_name,
                     const ai_quic_demo_options_t *options) {
  if (options->show_help) {
    ai_quic_demo_print_help(program_name, options->role);
    return 0;
  }

  ai_quic_log_set_level(AI_QUIC_LOG_INFO);
  if (options->self_check) {
    return ai_quic_demo_run_self_check(options);
  }

  if (strcmp(options->role, "server") == 0) {
    return ai_quic_demo_run_server(options);
  }
  return ai_quic_demo_run_client(options);
}
