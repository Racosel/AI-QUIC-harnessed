#include "transport_internal.h"

#include <openssl/aead.h>
#include <openssl/aes.h>
#include <openssl/chacha.h>
#include <openssl/evp.h>
#include <openssl/hkdf.h>
#include <openssl/tls1.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common_internal.h"

static char g_ai_quic_packet_decode_error[256];
static char g_ai_quic_packet_encode_error[256];

static void ai_quic_packet_decode_set_error(const char *format, ...) {
  va_list args;

  if (format == NULL) {
    g_ai_quic_packet_decode_error[0] = '\0';
    return;
  }

  va_start(args, format);
  vsnprintf(g_ai_quic_packet_decode_error,
            sizeof(g_ai_quic_packet_decode_error),
            format,
            args);
  va_end(args);
}

const char *ai_quic_packet_decode_last_error(void) {
  return g_ai_quic_packet_decode_error;
}

static void ai_quic_packet_encode_set_error(const char *format, ...) {
  va_list args;

  if (format == NULL) {
    g_ai_quic_packet_encode_error[0] = '\0';
    return;
  }

  va_start(args, format);
  vsnprintf(g_ai_quic_packet_encode_error,
            sizeof(g_ai_quic_packet_encode_error),
            format,
            args);
  va_end(args);
}

const char *ai_quic_packet_encode_last_error(void) {
  return g_ai_quic_packet_encode_error;
}

static ai_quic_result_t ai_quic_encode_frames(const ai_quic_packet_t *packet,
                                              uint8_t *payload,
                                              size_t capacity,
                                              size_t *payload_len) {
  size_t offset;
  size_t i;
  size_t chunk;

  offset = 0u;
  for (i = 0; i < packet->frame_count; ++i) {
    if (ai_quic_frame_encode(&packet->frames[i],
                             payload + offset,
                             capacity - offset,
                             &chunk) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    offset += chunk;
  }

  *payload_len = offset;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_packet_compute_retry_integrity_tag(
    ai_quic_version_t version,
    const ai_quic_cid_t *original_dcid,
    const uint8_t *retry_without_tag,
    size_t retry_without_tag_len,
    uint8_t tag[AI_QUIC_RETRY_INTEGRITY_TAG_LEN]) {
  const ai_quic_version_ops_t *ops;
  EVP_AEAD_CTX ctx;
  uint8_t pseudo_packet[1u + AI_QUIC_MAX_CID_LEN + AI_QUIC_MAX_PACKET_SIZE];
  size_t pseudo_len;
  size_t ciphertext_len;

  if (original_dcid == NULL || original_dcid->len == 0u || retry_without_tag == NULL ||
      tag == NULL || retry_without_tag_len > AI_QUIC_MAX_PACKET_SIZE ||
      retry_without_tag_len + 1u + original_dcid->len > sizeof(pseudo_packet)) {
    return AI_QUIC_ERROR;
  }

  ops = ai_quic_version_ops_find(version);
  if (ops == NULL) {
    return AI_QUIC_ERROR;
  }

  pseudo_len = 0u;
  pseudo_packet[pseudo_len++] = (uint8_t)original_dcid->len;
  memcpy(pseudo_packet + pseudo_len, original_dcid->bytes, original_dcid->len);
  pseudo_len += original_dcid->len;
  memcpy(pseudo_packet + pseudo_len, retry_without_tag, retry_without_tag_len);
  pseudo_len += retry_without_tag_len;

  EVP_AEAD_CTX_zero(&ctx);
  if (!EVP_AEAD_CTX_init(&ctx,
                         EVP_aead_aes_128_gcm(),
                         ops->retry_integrity_key,
                         sizeof(ops->retry_integrity_key),
                         AI_QUIC_RETRY_INTEGRITY_TAG_LEN,
                         NULL)) {
    return AI_QUIC_ERROR;
  }
  if (!EVP_AEAD_CTX_seal(&ctx,
                         tag,
                         &ciphertext_len,
                         AI_QUIC_RETRY_INTEGRITY_TAG_LEN,
                         ops->retry_integrity_nonce,
                         sizeof(ops->retry_integrity_nonce),
                         NULL,
                         0u,
                         pseudo_packet,
                         pseudo_len)) {
    EVP_AEAD_CTX_cleanup(&ctx);
    return AI_QUIC_ERROR;
  }
  EVP_AEAD_CTX_cleanup(&ctx);
  return ciphertext_len == AI_QUIC_RETRY_INTEGRITY_TAG_LEN ? AI_QUIC_OK : AI_QUIC_ERROR;
}

static ai_quic_result_t ai_quic_packet_verify_retry_integrity_tag(
    ai_quic_version_t version,
    const ai_quic_cid_t *original_dcid,
    const uint8_t *retry_packet,
    size_t retry_packet_len) {
  uint8_t expected_tag[AI_QUIC_RETRY_INTEGRITY_TAG_LEN];

  if (retry_packet == NULL || retry_packet_len < AI_QUIC_RETRY_INTEGRITY_TAG_LEN) {
    return AI_QUIC_ERROR;
  }
  if (ai_quic_packet_compute_retry_integrity_tag(version,
                                                 original_dcid,
                                                 retry_packet,
                                                 retry_packet_len -
                                                     AI_QUIC_RETRY_INTEGRITY_TAG_LEN,
                                                 expected_tag) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  return memcmp(expected_tag,
                retry_packet + retry_packet_len - AI_QUIC_RETRY_INTEGRITY_TAG_LEN,
                AI_QUIC_RETRY_INTEGRITY_TAG_LEN) == 0
             ? AI_QUIC_OK
             : AI_QUIC_ERROR;
}

ai_quic_result_t ai_quic_packet_encode(const ai_quic_packet_t *packet,
                                       uint8_t *buffer,
                                       size_t capacity,
                                       size_t *written) {
  uint8_t payload[AI_QUIC_MAX_PACKET_SIZE];
  size_t payload_len;
  size_t offset;
  size_t chunk;
  size_t i;

  if (packet == NULL || buffer == NULL || written == NULL || capacity == 0u) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  if (packet->header.type == AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION) {
    if (capacity < 7u + packet->header.dcid.len + packet->header.scid.len +
                       (packet->supported_versions_len * 4u)) {
      return AI_QUIC_ERROR;
    }
    buffer[offset++] = 0x80u;
    if (ai_quic_write_u32(buffer + offset, capacity - offset, &chunk, 0u) !=
        AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    offset += chunk;
    buffer[offset++] = (uint8_t)packet->header.dcid.len;
    memcpy(buffer + offset, packet->header.dcid.bytes, packet->header.dcid.len);
    offset += packet->header.dcid.len;
    buffer[offset++] = (uint8_t)packet->header.scid.len;
    memcpy(buffer + offset, packet->header.scid.bytes, packet->header.scid.len);
    offset += packet->header.scid.len;
    for (i = 0; i < packet->supported_versions_len; ++i) {
      if (ai_quic_write_u32(buffer + offset,
                            capacity - offset,
                            &chunk,
                            packet->supported_versions[i]) != AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      offset += chunk;
    }
    *written = offset;
    return AI_QUIC_OK;
  }

  if (packet->header.type == AI_QUIC_PACKET_TYPE_RETRY) {
    uint8_t tag[AI_QUIC_RETRY_INTEGRITY_TAG_LEN];

    if (capacity < 7u + packet->header.dcid.len + packet->header.scid.len +
                       packet->header.token_len + AI_QUIC_RETRY_INTEGRITY_TAG_LEN) {
      return AI_QUIC_ERROR;
    }
    if (ai_quic_version_encode_long_header_first_byte(packet->header.version,
                                                      packet->header.type,
                                                      1u,
                                                      &buffer[offset]) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    offset += 1u;
    if (ai_quic_write_u32(buffer + offset,
                          capacity - offset,
                          &chunk,
                          packet->header.version) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    offset += chunk;
    buffer[offset++] = (uint8_t)packet->header.dcid.len;
    memcpy(buffer + offset, packet->header.dcid.bytes, packet->header.dcid.len);
    offset += packet->header.dcid.len;
    buffer[offset++] = (uint8_t)packet->header.scid.len;
    memcpy(buffer + offset, packet->header.scid.bytes, packet->header.scid.len);
    offset += packet->header.scid.len;
    memcpy(buffer + offset, packet->header.token, packet->header.token_len);
    offset += packet->header.token_len;
    if (ai_quic_packet_compute_retry_integrity_tag(packet->header.version,
                                                   &packet->header.retry_original_dcid,
                                                   buffer,
                                                   offset,
                                                   tag) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    memcpy(buffer + offset, tag, sizeof(tag));
    offset += sizeof(tag);
    *written = offset;
    return AI_QUIC_OK;
  }

  if (ai_quic_encode_frames(packet, payload, sizeof(payload), &payload_len) !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  if (packet->header.type == AI_QUIC_PACKET_TYPE_ONE_RTT) {
    if (capacity < 1u + 1u + packet->header.dcid.len + 4u + payload_len) {
      return AI_QUIC_ERROR;
    }
    buffer[offset++] = 0x43u;
    buffer[offset++] = (uint8_t)packet->header.dcid.len;
    memcpy(buffer + offset, packet->header.dcid.bytes, packet->header.dcid.len);
    offset += packet->header.dcid.len;
    if (ai_quic_write_u32(buffer + offset,
                          capacity - offset,
                          &chunk,
                          (uint32_t)packet->header.packet_number) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    offset += chunk;
    memcpy(buffer + offset, payload, payload_len);
    offset += payload_len;
    *written = offset;
    return AI_QUIC_OK;
  }

  if (ai_quic_version_encode_long_header_first_byte(packet->header.version,
                                                    packet->header.type,
                                                    4u,
                                                    &buffer[offset]) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += 1u;
  if (ai_quic_write_u32(buffer + offset,
                        capacity - offset,
                        &chunk,
                        packet->header.version) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += chunk;
  buffer[offset++] = (uint8_t)packet->header.dcid.len;
  memcpy(buffer + offset, packet->header.dcid.bytes, packet->header.dcid.len);
  offset += packet->header.dcid.len;
  buffer[offset++] = (uint8_t)packet->header.scid.len;
  memcpy(buffer + offset, packet->header.scid.bytes, packet->header.scid.len);
  offset += packet->header.scid.len;

  if (packet->header.type == AI_QUIC_PACKET_TYPE_INITIAL) {
    if (ai_quic_varint_write(buffer + offset,
                             capacity - offset,
                             &chunk,
                             packet->header.token_len) != AI_QUIC_OK ||
        capacity - offset - chunk < packet->header.token_len) {
      return AI_QUIC_ERROR;
    }
    offset += chunk;
    memcpy(buffer + offset, packet->header.token, packet->header.token_len);
    offset += packet->header.token_len;
  }

  if (ai_quic_varint_write(buffer + offset,
                           capacity - offset,
                           &chunk,
                           payload_len + 4u) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += chunk;

  if (ai_quic_write_u32(buffer + offset,
                        capacity - offset,
                        &chunk,
                        (uint32_t)packet->header.packet_number) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += chunk;

  memcpy(buffer + offset, payload, payload_len);
  offset += payload_len;

  *written = offset;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_decode_frames_into(const uint8_t *payload,
                                                   size_t payload_len,
                                                   ai_quic_packet_t *packet) {
  size_t offset;
  size_t consumed;
  ai_quic_frame_t frame;

  offset = 0u;
  while (offset < payload_len) {
    if (ai_quic_frame_decode(payload + offset,
                             payload_len - offset,
                             &consumed,
                             &frame) != AI_QUIC_OK) {
      ai_quic_packet_decode_set_error("frame decode failed offset=%zu remaining=%zu first_byte=0x%02x",
                                      offset,
                                      payload_len - offset,
                                      payload[offset]);
      return AI_QUIC_ERROR;
    }
    if (frame.type != AI_QUIC_FRAME_PADDING) {
      if (packet->frame_count >= AI_QUIC_MAX_FRAMES_PER_PACKET) {
        ai_quic_packet_decode_set_error("frame count exceeded max=%u offset=%zu",
                                        (unsigned)AI_QUIC_MAX_FRAMES_PER_PACKET,
                                        offset);
        return AI_QUIC_ERROR;
      }
      packet->frames[packet->frame_count++] = frame;
    }
    offset += consumed;
  }
  return offset == payload_len ? AI_QUIC_OK : AI_QUIC_ERROR;
}

ai_quic_result_t ai_quic_packet_decode(const uint8_t *buffer,
                                       size_t buffer_len,
                                       size_t *consumed,
                                       ai_quic_packet_t *packet) {
  size_t offset;
  size_t chunk;
  uint32_t version;
  uint64_t payload_length;

  if (buffer == NULL || consumed == NULL || packet == NULL || buffer_len == 0u) {
    ai_quic_packet_decode_set_error("%s", "invalid decode arguments");
    return AI_QUIC_ERROR;
  }

  ai_quic_packet_decode_set_error("%s", "unspecified decode failure");
  memset(packet, 0, sizeof(*packet));
  offset = 0u;

  if ((buffer[0] & 0x80u) == 0u) {
    packet->header.type = AI_QUIC_PACKET_TYPE_ONE_RTT;
    offset += 1u;
    if (buffer_len < offset + 1u) {
      ai_quic_packet_decode_set_error("short header missing dcid_len byte remaining=%zu",
                                      buffer_len - offset + 1u);
      return AI_QUIC_ERROR;
    }
    if (!ai_quic_cid_from_bytes(&packet->header.dcid,
                                buffer + offset + 1u,
                                buffer[offset])) {
      ai_quic_packet_decode_set_error("short header invalid dcid_len=%u remaining=%zu",
                                      (unsigned int)buffer[offset],
                                      buffer_len - offset);
      return AI_QUIC_ERROR;
    }
    offset += 1u + buffer[offset];
    if (ai_quic_read_u32(buffer + offset, buffer_len - offset, &chunk, &version) !=
        AI_QUIC_OK) {
      ai_quic_packet_decode_set_error("short header packet number read failed offset=%zu remaining=%zu",
                                      offset,
                                      buffer_len - offset);
      return AI_QUIC_ERROR;
    }
    packet->header.packet_number = version;
    offset += chunk;
    if (ai_quic_decode_frames_into(buffer + offset, buffer_len - offset, packet) !=
        AI_QUIC_OK) {
      ai_quic_packet_decode_set_error("short header frame decode failed payload_offset=%zu payload_len=%zu",
                                      offset,
                                      buffer_len - offset);
      return AI_QUIC_ERROR;
    }
    packet->header.packet_length = buffer_len;
    packet->header.payload_length = buffer_len - offset;
    *consumed = buffer_len;
    return AI_QUIC_OK;
  }

  offset += 1u;
  if (ai_quic_read_u32(buffer + offset, buffer_len - offset, &chunk, &version) !=
      AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("long header version read failed first_byte=0x%02x remaining=%zu",
                                    buffer[0],
                                    buffer_len - offset);
    return AI_QUIC_ERROR;
  }
  packet->header.version = version;
  packet->header.type =
      version == 0u ? AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION
                    : ai_quic_version_decode_long_header_type(version, buffer[0]);
  offset += chunk;

  if (buffer_len < offset + 1u) {
    ai_quic_packet_decode_set_error("long header missing dcid_len byte offset=%zu len=%zu",
                                    offset,
                                    buffer_len);
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_cid_from_bytes(&packet->header.dcid,
                              buffer + offset + 1u,
                              buffer[offset])) {
    ai_quic_packet_decode_set_error("long header invalid dcid_len=%u offset=%zu remaining=%zu",
                                    (unsigned int)buffer[offset],
                                    offset,
                                    buffer_len - offset);
    return AI_QUIC_ERROR;
  }
  offset += 1u + buffer[offset];

  if (buffer_len < offset + 1u) {
    ai_quic_packet_decode_set_error("long header missing scid_len byte offset=%zu len=%zu",
                                    offset,
                                    buffer_len);
    return AI_QUIC_ERROR;
  }
  if (!ai_quic_cid_from_bytes(&packet->header.scid,
                              buffer + offset + 1u,
                              buffer[offset])) {
    ai_quic_packet_decode_set_error("long header invalid scid_len=%u offset=%zu remaining=%zu",
                                    (unsigned int)buffer[offset],
                                    offset,
                                    buffer_len - offset);
    return AI_QUIC_ERROR;
  }
  offset += 1u + buffer[offset];

  if (packet->header.version == 0u) {
    packet->header.type = AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION;
    while (offset + 4u <= buffer_len &&
           packet->supported_versions_len < AI_QUIC_MAX_SUPPORTED_VERSIONS) {
      if (ai_quic_read_u32(buffer + offset, buffer_len - offset, &chunk, &version) !=
          AI_QUIC_OK) {
        return AI_QUIC_ERROR;
      }
      packet->supported_versions[packet->supported_versions_len++] = version;
      offset += chunk;
    }
    packet->header.packet_length = buffer_len;
    *consumed = buffer_len;
    return AI_QUIC_OK;
  }

  if (packet->header.type == AI_QUIC_PACKET_TYPE_RETRY) {
    if (buffer_len < offset + AI_QUIC_RETRY_INTEGRITY_TAG_LEN) {
      ai_quic_packet_decode_set_error("%s", "retry packet missing integrity tag");
      return AI_QUIC_ERROR;
    }
    packet->header.token_len =
        buffer_len - offset - AI_QUIC_RETRY_INTEGRITY_TAG_LEN;
    if (packet->header.token_len > sizeof(packet->header.token)) {
      ai_quic_packet_decode_set_error("%s", "retry token exceeds buffer");
      return AI_QUIC_ERROR;
    }
    memcpy(packet->header.token, buffer + offset, packet->header.token_len);
    offset += packet->header.token_len + AI_QUIC_RETRY_INTEGRITY_TAG_LEN;
    packet->header.packet_length = offset;
    *consumed = offset;
    return AI_QUIC_OK;
  }

  if (packet->header.type == AI_QUIC_PACKET_TYPE_INITIAL) {
    if (ai_quic_varint_read(buffer + offset,
                            buffer_len - offset,
                            &chunk,
                            &payload_length) != AI_QUIC_OK ||
        payload_length > sizeof(packet->header.token) ||
        buffer_len - offset - chunk < payload_length) {
      ai_quic_packet_decode_set_error(
          "initial token parse failed offset=%zu remaining=%zu first_byte=0x%02x "
          "toy_decoder_expects_plaintext_token_varint",
          offset,
          buffer_len - offset,
          buffer[0]);
      return AI_QUIC_ERROR;
    }
    packet->header.token_len = (size_t)payload_length;
    offset += chunk;
    memcpy(packet->header.token, buffer + offset, packet->header.token_len);
    offset += packet->header.token_len;
  }

  if (ai_quic_varint_read(buffer + offset,
                          buffer_len - offset,
                          &chunk,
                          &payload_length) != AI_QUIC_OK ||
      payload_length < 4u ||
      buffer_len - offset - chunk < payload_length) {
    ai_quic_packet_decode_set_error(
        "payload length parse failed offset=%zu remaining=%zu first_byte=0x%02x "
        "toy_decoder_expects_varint_len_plus_4byte_pn",
        offset,
        buffer_len - offset,
        buffer[0]);
    return AI_QUIC_ERROR;
  }
  offset += chunk;

  if (ai_quic_read_u32(buffer + offset, buffer_len - offset, &chunk, &version) !=
      AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("packet number read failed offset=%zu remaining=%zu payload_length=%llu",
                                    offset,
                                    buffer_len - offset,
                                    (unsigned long long)payload_length);
    return AI_QUIC_ERROR;
  }
  packet->header.packet_number = version;
  offset += chunk;

  packet->header.payload_length = (size_t)payload_length - 4u;
  if (ai_quic_decode_frames_into(buffer + offset,
                                 packet->header.payload_length,
                                 packet) != AI_QUIC_OK) {
    ai_quic_packet_decode_set_error(
        "frame decode failed payload_offset=%zu payload_len=%zu first_byte=0x%02x "
        "payload_is_likely_protected_ciphertext",
        offset,
        packet->header.payload_length,
        buffer[0]);
    return AI_QUIC_ERROR;
  }
  offset += packet->header.payload_length;
  packet->header.packet_length = offset;
  *consumed = offset;
  return AI_QUIC_OK;
}

typedef enum ai_quic_hp_cipher {
  AI_QUIC_HP_CIPHER_AES = 0,
  AI_QUIC_HP_CIPHER_CHACHA20 = 1
} ai_quic_hp_cipher_t;

typedef struct ai_quic_packet_protection {
  const EVP_AEAD *aead;
  const EVP_MD *md;
  ai_quic_hp_cipher_t hp_cipher;
  size_t hp_key_len;
  uint8_t key[EVP_AEAD_MAX_KEY_LENGTH];
  size_t key_len;
  uint8_t iv[EVP_AEAD_MAX_NONCE_LENGTH];
  size_t iv_len;
  uint8_t hp[32];
} ai_quic_packet_protection_t;

static ai_quic_packet_number_space_id_t ai_quic_packet_type_to_space(
    ai_quic_packet_type_t type) {
  switch (type) {
    case AI_QUIC_PACKET_TYPE_INITIAL:
    case AI_QUIC_PACKET_TYPE_RETRY:
      return AI_QUIC_PN_SPACE_INITIAL;
    case AI_QUIC_PACKET_TYPE_HANDSHAKE:
      return AI_QUIC_PN_SPACE_HANDSHAKE;
    case AI_QUIC_PACKET_TYPE_ONE_RTT:
    default:
      return AI_QUIC_PN_SPACE_APP_DATA;
  }
}

static ai_quic_encryption_level_t ai_quic_packet_type_to_level(
    ai_quic_packet_type_t type) {
  switch (type) {
    case AI_QUIC_PACKET_TYPE_INITIAL:
    case AI_QUIC_PACKET_TYPE_RETRY:
      return AI_QUIC_ENCRYPTION_INITIAL;
    case AI_QUIC_PACKET_TYPE_HANDSHAKE:
      return AI_QUIC_ENCRYPTION_HANDSHAKE;
    case AI_QUIC_PACKET_TYPE_ONE_RTT:
    default:
      return AI_QUIC_ENCRYPTION_APPLICATION;
  }
}

static ai_quic_result_t ai_quic_quic_expand_label(const EVP_MD *md,
                                                  const uint8_t *secret,
                                                  size_t secret_len,
                                                  const char *label,
                                                  uint8_t *out,
                                                  size_t out_len) {
  static const uint8_t kPrefix[] = "tls13 ";
  uint8_t info[128];
  size_t label_len;
  size_t full_label_len;

  if (md == NULL || secret == NULL || label == NULL || out == NULL || out_len == 0u) {
    return AI_QUIC_ERROR;
  }

  label_len = strlen(label);
  full_label_len = sizeof(kPrefix) - 1u + label_len;
  if (full_label_len > 255u || 4u + full_label_len > sizeof(info) ||
      out_len > 0xffffu) {
    return AI_QUIC_ERROR;
  }

  info[0] = (uint8_t)((out_len >> 8u) & 0xffu);
  info[1] = (uint8_t)(out_len & 0xffu);
  info[2] = (uint8_t)full_label_len;
  memcpy(info + 3u, kPrefix, sizeof(kPrefix) - 1u);
  memcpy(info + 3u + sizeof(kPrefix) - 1u, label, label_len);
  info[3u + full_label_len] = 0u;

  return HKDF_expand(out,
                     out_len,
                     md,
                     secret,
                     secret_len,
                     info,
                     4u + full_label_len)
                 ? AI_QUIC_OK
                 : AI_QUIC_ERROR;
}

static ai_quic_result_t ai_quic_derive_initial_traffic_secret(
    ai_quic_version_t version,
    const ai_quic_cid_t *original_dcid,
    int is_server_secret,
    uint8_t *secret,
    size_t secret_len) {
  const ai_quic_version_ops_t *ops;
  uint8_t initial_secret[EVP_MAX_MD_SIZE];
  size_t initial_secret_len;

  if (original_dcid == NULL || secret == NULL || secret_len == 0u) {
    return AI_QUIC_ERROR;
  }
  ops = ai_quic_version_ops_find(version);
  if (ops == NULL) {
    return AI_QUIC_ERROR;
  }

  if (!HKDF_extract(initial_secret,
                    &initial_secret_len,
                    EVP_sha256(),
                    original_dcid->bytes,
                    original_dcid->len,
                    ops->initial_salt,
                    sizeof(ops->initial_salt))) {
    return AI_QUIC_ERROR;
  }

  return ai_quic_quic_expand_label(EVP_sha256(),
                                   initial_secret,
                                   initial_secret_len,
                                   is_server_secret ? "server in" : "client in",
                                   secret,
                                   secret_len);
}

static ai_quic_result_t ai_quic_select_cipher(uint32_t cipher_suite,
                                              const EVP_AEAD **aead,
                                              const EVP_MD **md,
                                              ai_quic_hp_cipher_t *hp_cipher,
                                              size_t *hp_key_len) {
  uint32_t suite_id;

  if (aead == NULL || md == NULL || hp_cipher == NULL || hp_key_len == NULL) {
    return AI_QUIC_ERROR;
  }

  /*
   * BoringSSL's SSL_CIPHER_get_protocol_id() reports TLS 1.3 cipher suites as
   * the wire value (for example 0x1301), while the public TLS1_3_CK_* macros
   * include the historical 0x03000000 prefix. Accept both forms here so packet
   * protection can consume secrets exported by the QUIC TLS callbacks.
   */
  suite_id = cipher_suite & 0xffffu;

  switch (suite_id) {
    case 0x1301u:
      *aead = EVP_aead_aes_128_gcm();
      *md = EVP_sha256();
      *hp_cipher = AI_QUIC_HP_CIPHER_AES;
      *hp_key_len = 16u;
      return AI_QUIC_OK;
    case 0x1302u:
      *aead = EVP_aead_aes_256_gcm();
      *md = EVP_sha384();
      *hp_cipher = AI_QUIC_HP_CIPHER_AES;
      *hp_key_len = 32u;
      return AI_QUIC_OK;
    case 0x1303u:
      *aead = EVP_aead_chacha20_poly1305();
      *md = EVP_sha256();
      *hp_cipher = AI_QUIC_HP_CIPHER_CHACHA20;
      *hp_key_len = 32u;
      return AI_QUIC_OK;
    default:
      return AI_QUIC_ERROR;
  }
}

static ai_quic_result_t ai_quic_build_packet_protection(
    ai_quic_conn_impl_t *conn,
    ai_quic_version_t version,
    ai_quic_packet_type_t type,
    int is_write,
    const ai_quic_cid_t *initial_dcid,
    ai_quic_packet_protection_t *protection) {
  const ai_quic_version_ops_t *ops;
  uint8_t secret[64];
  size_t secret_len;
  uint32_t cipher_suite;

  if (conn == NULL || protection == NULL) {
    ai_quic_packet_encode_set_error("%s", "build protection invalid arguments");
    return AI_QUIC_ERROR;
  }

  memset(protection, 0, sizeof(*protection));
  ops = ai_quic_version_ops_find(version);
  if (ops == NULL) {
    ai_quic_packet_encode_set_error("unsupported version=0x%08x", (unsigned)version);
    return AI_QUIC_ERROR;
  }
  if (type == AI_QUIC_PACKET_TYPE_INITIAL) {
    ai_quic_cid_t secret_dcid;
    int want_server_secret;

    memset(&secret_dcid, 0, sizeof(secret_dcid));
    if (conn->current_initial_dcid.len > 0u) {
      secret_dcid = conn->current_initial_dcid;
    } else if (conn->original_destination_cid.len > 0u) {
      secret_dcid = conn->original_destination_cid;
    } else if (initial_dcid != NULL) {
      secret_dcid = *initial_dcid;
    } else {
      ai_quic_packet_encode_set_error("%s", "initial secret dcid unavailable");
      return AI_QUIC_ERROR;
    }

    cipher_suite = TLS1_CK_AES_128_GCM_SHA256;
    if (ai_quic_select_cipher(cipher_suite,
                              &protection->aead,
                              &protection->md,
                              &protection->hp_cipher,
                              &protection->hp_key_len) != AI_QUIC_OK) {
      ai_quic_packet_encode_set_error("initial cipher suite unsupported=0x%08x",
                                      (unsigned)cipher_suite);
      return AI_QUIC_ERROR;
    }

    secret_len = (size_t)EVP_MD_size(protection->md);
    want_server_secret = conn->is_server == is_write;
    if (ai_quic_derive_initial_traffic_secret(version,
                                              &secret_dcid,
                                              want_server_secret,
                                              secret,
                                              secret_len) != AI_QUIC_OK) {
      ai_quic_packet_encode_set_error("%s", "initial traffic secret derivation failed");
      return AI_QUIC_ERROR;
    }
  } else {
    if (ai_quic_tls_session_get_packet_secret(conn->tls_session,
                                              ai_quic_packet_type_to_level(type),
                                              is_write,
                                              &cipher_suite,
                                              secret,
                                              &secret_len,
                                              sizeof(secret)) != AI_QUIC_OK ||
        ai_quic_select_cipher(cipher_suite,
                              &protection->aead,
                              &protection->md,
                              &protection->hp_cipher,
                              &protection->hp_key_len) != AI_QUIC_OK) {
      ai_quic_packet_encode_set_error("packet secret unavailable type=%d cipher=0x%08x",
                                      (int)type,
                                      (unsigned)cipher_suite);
      return AI_QUIC_ERROR;
    }
  }

  protection->key_len = EVP_AEAD_key_length(protection->aead);
  protection->iv_len = EVP_AEAD_nonce_length(protection->aead);
  if (protection->key_len > sizeof(protection->key) ||
      protection->iv_len > sizeof(protection->iv) ||
      protection->hp_key_len > sizeof(protection->hp) ||
      ai_quic_quic_expand_label(protection->md,
                                secret,
                                secret_len,
                                ops->key_label,
                                protection->key,
                                protection->key_len) != AI_QUIC_OK ||
      ai_quic_quic_expand_label(protection->md,
                                secret,
                                secret_len,
                                ops->iv_label,
                                protection->iv,
                                protection->iv_len) != AI_QUIC_OK ||
      ai_quic_quic_expand_label(protection->md,
                                secret,
                                secret_len,
                                ops->hp_label,
                                protection->hp,
                                protection->hp_key_len) != AI_QUIC_OK) {
    ai_quic_packet_encode_set_error("quic key derivation failed type=%d cipher=0x%08x",
                                    (int)type,
                                    (unsigned)cipher_suite);
    return AI_QUIC_ERROR;
  }

  return AI_QUIC_OK;
}

static void ai_quic_build_nonce(const ai_quic_packet_protection_t *protection,
                                uint64_t packet_number,
                                uint8_t *nonce) {
  uint8_t pn[8];
  size_t i;

  memcpy(nonce, protection->iv, protection->iv_len);
  for (i = 0; i < sizeof(pn); ++i) {
    pn[sizeof(pn) - 1u - i] = (uint8_t)(packet_number & 0xffu);
    packet_number >>= 8u;
  }
  for (i = 0; i < sizeof(pn) && i < protection->iv_len; ++i) {
    nonce[protection->iv_len - sizeof(pn) + i] ^= pn[i];
  }
}

static ai_quic_result_t ai_quic_make_hp_mask(
    const ai_quic_packet_protection_t *protection,
    const uint8_t *sample,
    uint8_t mask[5]) {
  if (protection == NULL || sample == NULL || mask == NULL) {
    return AI_QUIC_ERROR;
  }

  if (protection->hp_cipher == AI_QUIC_HP_CIPHER_AES) {
    AES_KEY aes_key;
    uint8_t block[16];

    if (AES_set_encrypt_key(protection->hp, (unsigned)(protection->hp_key_len * 8u),
                            &aes_key) != 0) {
      return AI_QUIC_ERROR;
    }
    AES_encrypt(sample, block, &aes_key);
    memcpy(mask, block, 5u);
    return AI_QUIC_OK;
  }

  if (protection->hp_cipher == AI_QUIC_HP_CIPHER_CHACHA20) {
    static const uint8_t kZeros[5] = {0u, 0u, 0u, 0u, 0u};
    uint8_t nonce[12];
    uint32_t counter;

    counter = (uint32_t)sample[0] | ((uint32_t)sample[1] << 8u) |
              ((uint32_t)sample[2] << 16u) | ((uint32_t)sample[3] << 24u);
    memcpy(nonce, sample + 4u, sizeof(nonce));
    CRYPTO_chacha_20(mask, kZeros, sizeof(kZeros), protection->hp, nonce, counter);
    return AI_QUIC_OK;
  }

  return AI_QUIC_ERROR;
}

static uint64_t ai_quic_decode_packet_number_value(uint64_t largest_pn,
                                                   uint64_t truncated_pn,
                                                   unsigned pn_nbits) {
  uint64_t expected_pn;
  uint64_t pn_win;
  uint64_t pn_hwin;
  uint64_t pn_mask;
  uint64_t candidate_pn;

  expected_pn = largest_pn == UINT64_MAX ? 0u : largest_pn + 1u;
  pn_win = (uint64_t)1u << pn_nbits;
  pn_hwin = pn_win / 2u;
  pn_mask = pn_win - 1u;
  candidate_pn = (expected_pn & ~pn_mask) | truncated_pn;

  if (candidate_pn + pn_hwin <= expected_pn &&
      candidate_pn < ((uint64_t)1u << 62u) - pn_win) {
    return candidate_pn + pn_win;
  }
  if (candidate_pn > expected_pn + pn_hwin && candidate_pn >= pn_win) {
    return candidate_pn - pn_win;
  }
  return candidate_pn;
}

static ai_quic_result_t ai_quic_packet_encode_long_protected(
    ai_quic_conn_impl_t *conn,
    const ai_quic_packet_t *packet,
    uint8_t *buffer,
    size_t capacity,
    size_t *written) {
  ai_quic_packet_protection_t protection;
  uint8_t plaintext[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t nonce[EVP_AEAD_MAX_NONCE_LENGTH];
  uint8_t mask[5];
  EVP_AEAD_CTX ctx;
  size_t plaintext_len;
  size_t ciphertext_len;
  size_t header_offset;
  size_t pn_offset;
  size_t length_len;
  size_t payload_length_value;
  size_t chunk;
  const size_t pn_len = 4u;
  const size_t tag_len = 16u;

  if (ai_quic_encode_frames(packet, plaintext, sizeof(plaintext), &plaintext_len) !=
      AI_QUIC_OK ||
      ai_quic_build_packet_protection(conn,
                                      packet->header.version,
                                      packet->header.type,
                                      1,
                                      &conn->original_destination_cid,
                                      &protection) != AI_QUIC_OK) {
    if (g_ai_quic_packet_encode_error[0] == '\0') {
      ai_quic_packet_encode_set_error("encode frames failed type=%d", (int)packet->header.type);
    }
    return AI_QUIC_ERROR;
  }

  payload_length_value = pn_len + plaintext_len + tag_len;
  header_offset = 0u;
  if (capacity < 7u + packet->header.dcid.len + packet->header.scid.len + pn_len + tag_len) {
    ai_quic_packet_encode_set_error("long header capacity too small type=%d cap=%zu",
                                    (int)packet->header.type,
                                    capacity);
    return AI_QUIC_ERROR;
  }

  if (ai_quic_version_encode_long_header_first_byte(packet->header.version,
                                                    packet->header.type,
                                                    (uint8_t)pn_len,
                                                    &buffer[header_offset]) != AI_QUIC_OK) {
    ai_quic_packet_encode_set_error("unsupported long header type=%d version=0x%08x",
                                    (int)packet->header.type,
                                    (unsigned)packet->header.version);
    return AI_QUIC_ERROR;
  }
  header_offset += 1u;
  if (ai_quic_write_u32(buffer + header_offset,
                        capacity - header_offset,
                        &chunk,
                        packet->header.version) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  header_offset += chunk;
  buffer[header_offset++] = (uint8_t)packet->header.dcid.len;
  memcpy(buffer + header_offset, packet->header.dcid.bytes, packet->header.dcid.len);
  header_offset += packet->header.dcid.len;
  buffer[header_offset++] = (uint8_t)packet->header.scid.len;
  memcpy(buffer + header_offset, packet->header.scid.bytes, packet->header.scid.len);
  header_offset += packet->header.scid.len;

  if (packet->header.type == AI_QUIC_PACKET_TYPE_INITIAL) {
    if (ai_quic_varint_write(buffer + header_offset,
                             capacity - header_offset,
                             &chunk,
                             packet->header.token_len) != AI_QUIC_OK ||
        capacity - header_offset - chunk < packet->header.token_len) {
      return AI_QUIC_ERROR;
    }
    header_offset += chunk;
    memcpy(buffer + header_offset, packet->header.token, packet->header.token_len);
    header_offset += packet->header.token_len;
  }

  if (ai_quic_varint_write(buffer + header_offset,
                           capacity - header_offset,
                           &length_len,
                           payload_length_value) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  header_offset += length_len;
  pn_offset = header_offset;
  buffer[header_offset++] = (uint8_t)((packet->header.packet_number >> 24u) & 0xffu);
  buffer[header_offset++] = (uint8_t)((packet->header.packet_number >> 16u) & 0xffu);
  buffer[header_offset++] = (uint8_t)((packet->header.packet_number >> 8u) & 0xffu);
  buffer[header_offset++] = (uint8_t)(packet->header.packet_number & 0xffu);

  EVP_AEAD_CTX_zero(&ctx);
  ai_quic_build_nonce(&protection, packet->header.packet_number, nonce);
  if (!EVP_AEAD_CTX_init(&ctx,
                         protection.aead,
                         protection.key,
                         protection.key_len,
                         EVP_AEAD_DEFAULT_TAG_LENGTH,
                         NULL)) {
    ai_quic_packet_encode_set_error("aead init failed type=%d", (int)packet->header.type);
    return AI_QUIC_ERROR;
  }
  if (!EVP_AEAD_CTX_seal(&ctx,
                         buffer + header_offset,
                         &ciphertext_len,
                         capacity - header_offset,
                         nonce,
                         protection.iv_len,
                         plaintext,
                         plaintext_len,
                         buffer,
                         header_offset)) {
    EVP_AEAD_CTX_cleanup(&ctx);
    ai_quic_packet_encode_set_error("aead seal failed type=%d plaintext_len=%zu",
                                    (int)packet->header.type,
                                    plaintext_len);
    return AI_QUIC_ERROR;
  }
  EVP_AEAD_CTX_cleanup(&ctx);

  if (header_offset + ciphertext_len < pn_offset + 4u + 16u ||
      ai_quic_make_hp_mask(&protection, buffer + pn_offset + 4u, mask) != AI_QUIC_OK) {
    ai_quic_packet_encode_set_error("header protection failed type=%d ciphertext_len=%zu",
                                    (int)packet->header.type,
                                    ciphertext_len);
    return AI_QUIC_ERROR;
  }
  buffer[0] ^= (uint8_t)(mask[0] & 0x0fu);
  buffer[pn_offset] ^= mask[1];
  buffer[pn_offset + 1u] ^= mask[2];
  buffer[pn_offset + 2u] ^= mask[3];
  buffer[pn_offset + 3u] ^= mask[4];

  *written = header_offset + ciphertext_len;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_packet_encode_short_protected(
    ai_quic_conn_impl_t *conn,
    const ai_quic_packet_t *packet,
    uint8_t *buffer,
    size_t capacity,
    size_t *written) {
  ai_quic_packet_protection_t protection;
  uint8_t plaintext[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t nonce[EVP_AEAD_MAX_NONCE_LENGTH];
  uint8_t mask[5];
  EVP_AEAD_CTX ctx;
  size_t plaintext_len;
  size_t ciphertext_len;
  size_t offset;
  size_t pn_offset;
  const size_t pn_len = 4u;

  if (conn == NULL || packet == NULL || buffer == NULL || written == NULL ||
      ai_quic_encode_frames(packet, plaintext, sizeof(plaintext), &plaintext_len) !=
          AI_QUIC_OK ||
      ai_quic_build_packet_protection(conn,
                                      packet->header.version,
                                      AI_QUIC_PACKET_TYPE_ONE_RTT,
                                      1,
                                      NULL,
                                      &protection) != AI_QUIC_OK) {
    if (g_ai_quic_packet_encode_error[0] == '\0') {
      ai_quic_packet_encode_set_error("%s", "short header setup failed");
    }
    return AI_QUIC_ERROR;
  }

  if (capacity < 1u + packet->header.dcid.len + pn_len + plaintext_len + 16u) {
    ai_quic_packet_encode_set_error("short header capacity too small cap=%zu", capacity);
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  buffer[offset++] = (uint8_t)(0x40u | ((pn_len - 1u) & 0x03u));
  memcpy(buffer + offset, packet->header.dcid.bytes, packet->header.dcid.len);
  offset += packet->header.dcid.len;
  pn_offset = offset;
  buffer[offset++] = (uint8_t)((packet->header.packet_number >> 24u) & 0xffu);
  buffer[offset++] = (uint8_t)((packet->header.packet_number >> 16u) & 0xffu);
  buffer[offset++] = (uint8_t)((packet->header.packet_number >> 8u) & 0xffu);
  buffer[offset++] = (uint8_t)(packet->header.packet_number & 0xffu);

  EVP_AEAD_CTX_zero(&ctx);
  ai_quic_build_nonce(&protection, packet->header.packet_number, nonce);
  if (!EVP_AEAD_CTX_init(&ctx,
                         protection.aead,
                         protection.key,
                         protection.key_len,
                         EVP_AEAD_DEFAULT_TAG_LENGTH,
                         NULL)) {
    ai_quic_packet_encode_set_error("%s", "short header aead init failed");
    return AI_QUIC_ERROR;
  }
  if (!EVP_AEAD_CTX_seal(&ctx,
                         buffer + offset,
                         &ciphertext_len,
                         capacity - offset,
                         nonce,
                         protection.iv_len,
                         plaintext,
                         plaintext_len,
                         buffer,
                         offset)) {
    EVP_AEAD_CTX_cleanup(&ctx);
    ai_quic_packet_encode_set_error("short header aead seal failed plaintext_len=%zu",
                                    plaintext_len);
    return AI_QUIC_ERROR;
  }
  EVP_AEAD_CTX_cleanup(&ctx);

  if (offset + ciphertext_len < pn_offset + 4u + 16u ||
      ai_quic_make_hp_mask(&protection, buffer + pn_offset + 4u, mask) != AI_QUIC_OK) {
    ai_quic_packet_encode_set_error("%s", "short header hp failed");
    return AI_QUIC_ERROR;
  }
  buffer[0] ^= (uint8_t)(mask[0] & 0x1fu);
  buffer[pn_offset] ^= mask[1];
  buffer[pn_offset + 1u] ^= mask[2];
  buffer[pn_offset + 2u] ^= mask[3];
  buffer[pn_offset + 3u] ^= mask[4];
  *written = offset + ciphertext_len;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_packet_encode_conn(ai_quic_conn_impl_t *conn,
                                            const ai_quic_packet_t *packet,
                                            uint8_t *buffer,
                                            size_t capacity,
                                            size_t *written) {
  ai_quic_packet_encode_set_error("%s", "unspecified encode failure");
  if (conn == NULL || packet == NULL || buffer == NULL || written == NULL) {
    ai_quic_packet_encode_set_error("%s", "invalid protected encode arguments");
    return AI_QUIC_ERROR;
  }
  if (packet->header.type == AI_QUIC_PACKET_TYPE_VERSION_NEGOTIATION ||
      packet->header.type == AI_QUIC_PACKET_TYPE_RETRY) {
    return ai_quic_packet_encode(packet, buffer, capacity, written);
  }
  if (packet->header.type == AI_QUIC_PACKET_TYPE_ONE_RTT) {
    return ai_quic_packet_encode_short_protected(conn, packet, buffer, capacity, written);
  }
  return ai_quic_packet_encode_long_protected(conn, packet, buffer, capacity, written);
}

static ai_quic_result_t ai_quic_packet_decode_long_protected(
    ai_quic_conn_impl_t *conn,
    const uint8_t *buffer,
    size_t buffer_len,
    size_t *consumed,
    ai_quic_packet_t *packet) {
  ai_quic_packet_protection_t protection;
  uint8_t header[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t plaintext[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t nonce[EVP_AEAD_MAX_NONCE_LENGTH];
  uint8_t mask[5];
  EVP_AEAD_CTX ctx;
  size_t offset;
  size_t chunk;
  size_t pn_offset;
  size_t packet_len;
  size_t ciphertext_len;
  size_t plaintext_len;
  uint64_t payload_length;
  uint64_t packet_number;
  uint32_t version;
  uint8_t pn_len;
  const ai_quic_packet_number_space_t *space;

  memset(packet, 0, sizeof(*packet));
  if (buffer_len < 7u) {
    ai_quic_packet_decode_set_error("%s", "protected long header too short");
    return AI_QUIC_ERROR;
  }

  offset = 1u;
  if (ai_quic_read_u32(buffer + offset, buffer_len - offset, &chunk, &version) !=
      AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("%s", "protected long header version read failed");
    return AI_QUIC_ERROR;
  }
  packet->header.version = version;
  offset += chunk;

  if (offset >= buffer_len ||
      !ai_quic_cid_from_bytes(&packet->header.dcid, buffer + offset + 1u, buffer[offset])) {
    ai_quic_packet_decode_set_error("%s", "protected long header dcid parse failed");
    return AI_QUIC_ERROR;
  }
  offset += 1u + buffer[offset];
  if (offset >= buffer_len ||
      !ai_quic_cid_from_bytes(&packet->header.scid, buffer + offset + 1u, buffer[offset])) {
    ai_quic_packet_decode_set_error("%s", "protected long header scid parse failed");
    return AI_QUIC_ERROR;
  }
  offset += 1u + buffer[offset];

  packet->header.type = ai_quic_version_decode_long_header_type(version, buffer[0]);
  if (packet->header.type == AI_QUIC_PACKET_TYPE_INITIAL) {
    if (ai_quic_varint_read(buffer + offset,
                            buffer_len - offset,
                            &chunk,
                            &payload_length) != AI_QUIC_OK ||
        payload_length > sizeof(packet->header.token) ||
        buffer_len - offset - chunk < payload_length) {
      ai_quic_packet_decode_set_error("%s", "protected initial token parse failed");
      return AI_QUIC_ERROR;
    }
    packet->header.token_len = (size_t)payload_length;
    offset += chunk;
    memcpy(packet->header.token, buffer + offset, packet->header.token_len);
    offset += packet->header.token_len;
  }

  if (ai_quic_varint_read(buffer + offset,
                          buffer_len - offset,
                          &chunk,
                          &payload_length) != AI_QUIC_OK ||
      buffer_len - offset - chunk < payload_length || payload_length < 1u + 16u) {
    ai_quic_packet_decode_set_error("%s", "protected long header length parse failed");
    return AI_QUIC_ERROR;
  }
  offset += chunk;
  pn_offset = offset;
  packet_len = pn_offset + (size_t)payload_length;
  if (packet_len > buffer_len || packet_len < pn_offset + 4u + 16u) {
    ai_quic_packet_decode_set_error("%s", "protected long header truncated");
    return AI_QUIC_ERROR;
  }

  if (ai_quic_build_packet_protection(conn,
                                      packet->header.version,
                                      packet->header.type,
                                      0,
                                      &packet->header.dcid,
                                      &protection) != AI_QUIC_OK ||
      ai_quic_make_hp_mask(&protection, buffer + pn_offset + 4u, mask) != AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("%s", "protected long header key setup failed");
    return AI_QUIC_ERROR;
  }

  memcpy(header, buffer, packet_len);
  header[0] ^= (uint8_t)(mask[0] & 0x0fu);
  pn_len = (uint8_t)((header[0] & 0x03u) + 1u);
  if (packet_len < pn_offset + pn_len + 16u) {
    ai_quic_packet_decode_set_error("%s", "protected long header packet number truncated");
    return AI_QUIC_ERROR;
  }
  header[pn_offset] ^= mask[1];
  if (pn_len > 1u) {
    header[pn_offset + 1u] ^= mask[2];
  }
  if (pn_len > 2u) {
    header[pn_offset + 2u] ^= mask[3];
  }
  if (pn_len > 3u) {
    header[pn_offset + 3u] ^= mask[4];
  }

  packet_number = 0u;
  for (chunk = 0u; chunk < pn_len; ++chunk) {
    packet_number = (packet_number << 8u) | header[pn_offset + chunk];
  }
  space = ai_quic_conn_space_const(conn, ai_quic_packet_type_to_space(packet->header.type));
  packet->header.packet_number =
      ai_quic_decode_packet_number_value(
          space != NULL ? space->largest_received_packet_number : UINT64_MAX,
                                         packet_number,
                                         (unsigned)(pn_len * 8u));

  ciphertext_len = packet_len - pn_offset - pn_len;
  ai_quic_build_nonce(&protection, packet->header.packet_number, nonce);
  EVP_AEAD_CTX_zero(&ctx);
  if (!EVP_AEAD_CTX_init(&ctx,
                         protection.aead,
                         protection.key,
                         protection.key_len,
                         EVP_AEAD_DEFAULT_TAG_LENGTH,
                         NULL)) {
    ai_quic_packet_decode_set_error("%s", "protected long header aead init failed");
    return AI_QUIC_ERROR;
  }
  if (!EVP_AEAD_CTX_open(&ctx,
                         plaintext,
                         &plaintext_len,
                         sizeof(plaintext),
                         nonce,
                         protection.iv_len,
                         header + pn_offset + pn_len,
                         ciphertext_len,
                         header,
                         pn_offset + pn_len)) {
    EVP_AEAD_CTX_cleanup(&ctx);
    ai_quic_packet_decode_set_error("%s", "protected long header decrypt failed");
    return AI_QUIC_ERROR;
  }
  EVP_AEAD_CTX_cleanup(&ctx);

  packet->header.payload_length = plaintext_len;
  packet->header.packet_length = packet_len;
  if (ai_quic_decode_frames_into(plaintext, plaintext_len, packet) != AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("protected long header frame decode failed: %s",
                                    g_ai_quic_packet_decode_error);
    return AI_QUIC_ERROR;
  }

  *consumed = packet_len;
  return AI_QUIC_OK;
}

static ai_quic_result_t ai_quic_packet_decode_short_protected(
    ai_quic_conn_impl_t *conn,
    const uint8_t *buffer,
    size_t buffer_len,
    size_t *consumed,
    ai_quic_packet_t *packet) {
  ai_quic_packet_protection_t protection;
  uint8_t header[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t plaintext[AI_QUIC_MAX_PACKET_SIZE];
  uint8_t nonce[EVP_AEAD_MAX_NONCE_LENGTH];
  uint8_t mask[5];
  EVP_AEAD_CTX ctx;
  uint64_t truncated_pn;
  size_t pn_offset;
  size_t ciphertext_len;
  size_t plaintext_len;
  uint8_t pn_len;
  size_t i;
  const ai_quic_packet_number_space_t *space;

  memset(packet, 0, sizeof(*packet));
  if (conn == NULL || conn->local_cid.len == 0u ||
      buffer_len < 1u + conn->local_cid.len + 4u + 16u) {
    ai_quic_packet_decode_set_error("%s", "protected short header too short");
    return AI_QUIC_ERROR;
  }

  packet->header.type = AI_QUIC_PACKET_TYPE_ONE_RTT;
  packet->header.version = conn->version;
  packet->header.dcid = conn->local_cid;
  pn_offset = 1u + conn->local_cid.len;
  if (ai_quic_build_packet_protection(conn,
                                      conn->version,
                                      AI_QUIC_PACKET_TYPE_ONE_RTT,
                                      0,
                                      NULL,
                                      &protection) != AI_QUIC_OK ||
      ai_quic_make_hp_mask(&protection, buffer + pn_offset + 4u, mask) != AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("%s", "protected short header key setup failed");
    return AI_QUIC_ERROR;
  }

  memcpy(header, buffer, buffer_len);
  header[0] ^= (uint8_t)(mask[0] & 0x1fu);
  pn_len = (uint8_t)((header[0] & 0x03u) + 1u);
  if (buffer_len < pn_offset + pn_len + 16u) {
    ai_quic_packet_decode_set_error("%s", "protected short header packet number truncated");
    return AI_QUIC_ERROR;
  }
  for (i = 0u; i < pn_len; ++i) {
    header[pn_offset + i] ^= mask[1u + i];
  }

  truncated_pn = 0u;
  for (i = 0u; i < pn_len; ++i) {
    truncated_pn = (truncated_pn << 8u) | header[pn_offset + i];
  }
  space = ai_quic_conn_space_const(conn, AI_QUIC_PN_SPACE_APP_DATA);
  packet->header.packet_number =
      ai_quic_decode_packet_number_value(
          space != NULL ? space->largest_received_packet_number : UINT64_MAX,
                                         truncated_pn,
                                         (unsigned)(pn_len * 8u));

  ciphertext_len = buffer_len - pn_offset - pn_len;
  ai_quic_build_nonce(&protection, packet->header.packet_number, nonce);
  EVP_AEAD_CTX_zero(&ctx);
  if (!EVP_AEAD_CTX_init(&ctx,
                         protection.aead,
                         protection.key,
                         protection.key_len,
                         EVP_AEAD_DEFAULT_TAG_LENGTH,
                         NULL)) {
    ai_quic_packet_decode_set_error("%s", "protected short header aead init failed");
    return AI_QUIC_ERROR;
  }
  if (!EVP_AEAD_CTX_open(&ctx,
                         plaintext,
                         &plaintext_len,
                         sizeof(plaintext),
                         nonce,
                         protection.iv_len,
                         header + pn_offset + pn_len,
                         ciphertext_len,
                         header,
                         pn_offset + pn_len)) {
    EVP_AEAD_CTX_cleanup(&ctx);
    ai_quic_packet_decode_set_error("%s", "protected short header decrypt failed");
    return AI_QUIC_ERROR;
  }
  EVP_AEAD_CTX_cleanup(&ctx);

  packet->header.payload_length = plaintext_len;
  packet->header.packet_length = buffer_len;
  if (ai_quic_decode_frames_into(plaintext, plaintext_len, packet) != AI_QUIC_OK) {
    ai_quic_packet_decode_set_error("protected short header frame decode failed: %s",
                                    g_ai_quic_packet_decode_error);
    return AI_QUIC_ERROR;
  }

  *consumed = buffer_len;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_packet_decode_conn(ai_quic_conn_impl_t *conn,
                                            const uint8_t *buffer,
                                            size_t buffer_len,
                                            size_t *consumed,
                                            ai_quic_packet_t *packet) {
  size_t chunk;
  uint32_t version;

  if (conn == NULL || buffer == NULL || consumed == NULL || packet == NULL ||
      buffer_len == 0u) {
    ai_quic_packet_decode_set_error("%s", "invalid protected decode arguments");
    return AI_QUIC_ERROR;
  }

  if ((buffer[0] & 0x80u) == 0u) {
    return ai_quic_packet_decode_short_protected(conn, buffer, buffer_len, consumed, packet);
  }
  if (buffer_len >= 5u &&
      ai_quic_read_u32(buffer + 1u, buffer_len - 1u, &chunk, &version) == AI_QUIC_OK &&
      version == 0u) {
    return ai_quic_packet_decode(buffer, buffer_len, consumed, packet);
  }
  if (buffer_len >= 5u &&
      ai_quic_read_u32(buffer + 1u, buffer_len - 1u, &chunk, &version) == AI_QUIC_OK &&
      ai_quic_version_decode_long_header_type(version, buffer[0]) ==
          AI_QUIC_PACKET_TYPE_RETRY) {
    if (ai_quic_packet_decode(buffer, buffer_len, consumed, packet) != AI_QUIC_OK) {
      return AI_QUIC_ERROR;
    }
    if (conn->retry_accepted ||
        ai_quic_packet_verify_retry_integrity_tag(packet->header.version,
                                                  &conn->current_initial_dcid,
                                                  buffer,
                                                  *consumed) != AI_QUIC_OK) {
      ai_quic_packet_decode_set_error("%s", "retry integrity verification failed");
      return AI_QUIC_ERROR;
    }
    return AI_QUIC_OK;
  }
  return ai_quic_packet_decode_long_protected(conn, buffer, buffer_len, consumed, packet);
}
