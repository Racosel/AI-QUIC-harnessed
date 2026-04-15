#ifndef AI_QUIC_DEMO_CLI_H
#define AI_QUIC_DEMO_CLI_H

typedef struct ai_quic_demo_options {
  const char *role;
  const char *log_file;
  const char *qlog_dir;
  const char *ssl_keylog_file;
  const char *cert_dir;
  const char *www_dir;
  const char *downloads_dir;
  const char *requests;
  const char *testcase;
  int self_check;
  int show_help;
} ai_quic_demo_options_t;

int ai_quic_demo_parse_options(int argc,
                               char **argv,
                               const char *role,
                               ai_quic_demo_options_t *options);
int ai_quic_demo_run(const char *program_name,
                     const ai_quic_demo_options_t *options);
void ai_quic_demo_print_help(const char *program_name, const char *role);

#endif
