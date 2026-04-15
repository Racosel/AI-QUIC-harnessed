#include "demo_cli.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "ai_quic/conn.h"
#include "ai_quic/dispatcher.h"
#include "ai_quic/fs.h"
#include "ai_quic/log.h"
#include "ai_quic/result.h"
#include "ai_quic/tls.h"
#include "ai_quic/version.h"

static ai_quic_result_t ai_quic_demo_write_artifacts(
    const ai_quic_demo_options_t *options,
    const char *program_name) {
  char log_message[1024];
  char qlog_note_path[1024];
  const char *log_path;
  const char *qlog_dir;
  const char *ssl_keylog_file;

  log_path =
      options->log_file != NULL ? options->log_file : "/tmp/ai_quic_demo.log";
  qlog_dir = options->qlog_dir != NULL ? options->qlog_dir : "/tmp/ai_quic_qlog";
  ssl_keylog_file = options->ssl_keylog_file != NULL ? options->ssl_keylog_file
                                                     : "/tmp/ai_quic_keys.log";

  snprintf(log_message,
           sizeof(log_message),
           "program=%s\nrole=%s\ntestcase=%s\nrequests=%s\nnote=foundation "
           "stage placeholder only\n",
           program_name,
           options->role != NULL ? options->role : "unknown",
           options->testcase != NULL ? options->testcase : "(unset)",
           options->requests != NULL ? options->requests : "(unset)");
  if (ai_quic_fs_write_text_file(log_path, log_message) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_fs_ensure_dir(qlog_dir) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  snprintf(qlog_note_path, sizeof(qlog_note_path), "%s/%s", qlog_dir, "README.txt");
  if (ai_quic_fs_write_text_file(
          qlog_note_path,
          "AI-QUIC foundation stage: qlog directory created, no qlog events yet.\n") !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_fs_touch_file(
          ssl_keylog_file,
          "# AI-QUIC foundation stage: no TLS secrets exported yet.\n") !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  return AI_QUIC_OK;
}

void ai_quic_demo_print_help(const char *program_name, const char *role) {
  printf("%s (%s)\n", program_name, role);
  printf("All public and internal symbols use the ai_quic_* prefix.\n");
  printf("Usage:\n");
  printf("  %s --help\n", program_name);
  printf("  %s --self-check [options]\n", program_name);
  printf("\nOptions:\n");
  printf("  --log-file PATH\n");
  printf("  --qlog-dir PATH\n");
  printf("  --ssl-keylog-file PATH\n");
  printf("  --cert-dir PATH\n");
  printf("  --www PATH\n");
  printf("  --downloads PATH\n");
  printf("  --requests STRING\n");
  printf("  --testcase NAME\n");
}

int ai_quic_demo_parse_options(int argc,
                               char **argv,
                               const char *role,
                               ai_quic_demo_options_t *options) {
  int opt;
  int option_index;
  static struct option ai_quic_demo_long_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"self-check", no_argument, NULL, 's'},
      {"log-file", required_argument, NULL, 'l'},
      {"qlog-dir", required_argument, NULL, 'q'},
      {"ssl-keylog-file", required_argument, NULL, 'k'},
      {"cert-dir", required_argument, NULL, 'c'},
      {"www", required_argument, NULL, 'w'},
      {"downloads", required_argument, NULL, 'd'},
      {"requests", required_argument, NULL, 'r'},
      {"testcase", required_argument, NULL, 't'},
      {NULL, 0, NULL, 0}};

  memset(options, 0, sizeof(*options));
  options->role = role;

  optind = 1;
  while ((opt = getopt_long(argc,
                            argv,
                            "hsl:q:k:c:w:d:r:t:",
                            ai_quic_demo_long_options,
                            &option_index)) != -1) {
    (void)option_index;
    switch (opt) {
      case 'h':
        options->show_help = 1;
        break;
      case 's':
        options->self_check = 1;
        break;
      case 'l':
        options->log_file = optarg;
        break;
      case 'q':
        options->qlog_dir = optarg;
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
      case 'r':
        options->requests = optarg;
        break;
      case 't':
        options->testcase = optarg;
        break;
      default:
        return 1;
    }
  }

  return 0;
}

int ai_quic_demo_run(const char *program_name,
                     const ai_quic_demo_options_t *options) {
  ai_quic_dispatcher_t *dispatcher;
  ai_quic_conn_t *conn;
  ai_quic_result_t tls_result;

  if (options->show_help) {
    ai_quic_demo_print_help(program_name, options->role);
    return 0;
  }

  if (!options->self_check) {
    fprintf(stderr,
            "%s: foundation stage only supports --help or --self-check.\n",
            program_name);
    return 1;
  }

  ai_quic_log_set_level(AI_QUIC_LOG_INFO);
  ai_quic_log_write(AI_QUIC_LOG_INFO,
                    options->role != NULL ? options->role : "demo",
                    "starting foundation self-check with %s backend",
                    ai_quic_tls_backend_name());

  dispatcher = ai_quic_dispatcher_create();
  if (dispatcher == NULL) {
    return 1;
  }

  conn = ai_quic_conn_create(AI_QUIC_VERSION_V1);
  if (conn == NULL) {
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  tls_result = ai_quic_tls_self_check();
  if (tls_result != AI_QUIC_OK) {
    ai_quic_conn_destroy(conn);
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  if (ai_quic_demo_write_artifacts(options, program_name) != AI_QUIC_OK) {
    ai_quic_conn_destroy(conn);
    ai_quic_dispatcher_destroy(dispatcher);
    return 1;
  }

  ai_quic_conn_destroy(conn);
  ai_quic_dispatcher_destroy(dispatcher);
  return 0;
}
