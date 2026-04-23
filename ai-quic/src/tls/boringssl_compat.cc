#include <openssl/ssl.h>

extern "C" void ai_quic_boringssl_set_aes_hw_override(SSL_CTX *ctx,
                                                       int override_value) {
  bssl::SSL_CTX_set_aes_hw_override_for_testing(ctx, override_value != 0);
}

extern "C" void ai_quic_boringssl_set_tls13_chacha20_only(SSL_CTX *ctx,
                                                           int enabled) {
  bssl::SSL_CTX_set_tls13_chacha20_only_for_testing(ctx, enabled != 0);
}
