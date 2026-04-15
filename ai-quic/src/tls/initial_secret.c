#include "tls_internal.h"

#include <openssl/evp.h>
#include <string.h>

ai_quic_result_t ai_quic_tls_generate_initial_secret(const uint8_t *seed,
                                                     size_t seed_len,
                                                     uint8_t *secret,
                                                     size_t secret_len) {
  EVP_MD_CTX *ctx;
  unsigned int digest_len;
  uint8_t digest[EVP_MAX_MD_SIZE];
  size_t copied;

  if (seed == NULL || secret == NULL || secret_len == 0u) {
    return AI_QUIC_ERROR;
  }

  ctx = EVP_MD_CTX_new();
  if (ctx == NULL) {
    return AI_QUIC_ERROR;
  }

  if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
      EVP_DigestUpdate(ctx, seed, seed_len) != 1 ||
      EVP_DigestUpdate(ctx, "ai-quic-initial", 15u) != 1 ||
      EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return AI_QUIC_ERROR;
  }

  copied = secret_len < digest_len ? secret_len : digest_len;
  memcpy(secret, digest, copied);
  EVP_MD_CTX_free(ctx);
  return AI_QUIC_OK;
}
