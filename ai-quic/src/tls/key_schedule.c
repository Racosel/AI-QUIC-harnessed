#include "tls_internal.h"

#include <openssl/evp.h>
#include <string.h>

ai_quic_result_t ai_quic_tls_write_keylog(ai_quic_tls_session_t *session,
                                          const char *label,
                                          const uint8_t *secret,
                                          size_t secret_len) {
  size_t i;

  if (session == NULL || label == NULL || secret == NULL) {
    return AI_QUIC_ERROR;
  }
  if (session->keylog_file == NULL) {
    return AI_QUIC_OK;
  }

  fprintf(session->keylog_file, "%s ", label);
  for (i = 0; i < secret_len; ++i) {
    fprintf(session->keylog_file, "%02x", secret[i]);
  }
  fputc('\n', session->keylog_file);
  fflush(session->keylog_file);
  return AI_QUIC_OK;
}
