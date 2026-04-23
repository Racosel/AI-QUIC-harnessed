#include "transport_internal.h"

#include <openssl/hmac.h>
#include <string.h>

#include "common_internal.h"

#define AI_QUIC_RETRY_TOKEN_FORMAT_VERSION 0x01u
#define AI_QUIC_RETRY_TOKEN_MAC_LEN 16u

static ai_quic_result_t ai_quic_retry_token_mac(
    const uint8_t key[AI_QUIC_RETRY_TOKEN_KEY_LEN],
    const uint8_t *input,
    size_t input_len,
    uint8_t mac[AI_QUIC_RETRY_TOKEN_MAC_LEN]) {
  unsigned mac_len;
  uint8_t full_mac[EVP_MAX_MD_SIZE];

  if (key == NULL || input == NULL || mac == NULL) {
    return AI_QUIC_ERROR;
  }

  if (HMAC(EVP_sha256(),
           key,
           AI_QUIC_RETRY_TOKEN_KEY_LEN,
           input,
           input_len,
           full_mac,
           &mac_len) == NULL ||
      mac_len < AI_QUIC_RETRY_TOKEN_MAC_LEN) {
    return AI_QUIC_ERROR;
  }

  memcpy(mac, full_mac, AI_QUIC_RETRY_TOKEN_MAC_LEN);
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_retry_token_generate(
    const uint8_t key[AI_QUIC_RETRY_TOKEN_KEY_LEN],
    const uint8_t *peer_addr,
    size_t peer_addr_len,
    const ai_quic_cid_t *original_destination_cid,
    const ai_quic_cid_t *retry_source_cid,
    uint64_t now_ms,
    uint8_t *token,
    size_t capacity,
    size_t *token_len) {
  size_t offset;
  size_t chunk;

  if (key == NULL || original_destination_cid == NULL || retry_source_cid == NULL ||
      token == NULL || token_len == NULL ||
      (peer_addr_len > 0u && peer_addr == NULL) ||
      peer_addr_len > AI_QUIC_RETRY_TOKEN_MAX_PEER_ADDR_LEN ||
      original_destination_cid->len == 0u || retry_source_cid->len == 0u) {
    return AI_QUIC_ERROR;
  }

  offset = 1u + 8u + 1u + peer_addr_len + 1u + original_destination_cid->len + 1u +
           retry_source_cid->len + AI_QUIC_RETRY_TOKEN_MAC_LEN;
  if (capacity < offset) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  token[offset++] = AI_QUIC_RETRY_TOKEN_FORMAT_VERSION;
  if (ai_quic_write_u64(
          token + offset, capacity - offset, &chunk, now_ms + AI_QUIC_RETRY_TOKEN_LIFETIME_MS) !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += chunk;
  token[offset++] = (uint8_t)peer_addr_len;
  if (peer_addr_len > 0u) {
    memcpy(token + offset, peer_addr, peer_addr_len);
    offset += peer_addr_len;
  }
  token[offset++] = (uint8_t)original_destination_cid->len;
  memcpy(token + offset,
         original_destination_cid->bytes,
         original_destination_cid->len);
  offset += original_destination_cid->len;
  token[offset++] = (uint8_t)retry_source_cid->len;
  memcpy(token + offset, retry_source_cid->bytes, retry_source_cid->len);
  offset += retry_source_cid->len;
  if (ai_quic_retry_token_mac(key, token, offset, token + offset) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += AI_QUIC_RETRY_TOKEN_MAC_LEN;
  *token_len = offset;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_retry_token_validate(
    const uint8_t key[AI_QUIC_RETRY_TOKEN_KEY_LEN],
    const uint8_t *token,
    size_t token_len,
    const uint8_t *peer_addr,
    size_t peer_addr_len,
    const ai_quic_cid_t *packet_dcid,
    uint64_t now_ms,
    ai_quic_retry_token_metadata_t *metadata) {
  size_t offset;
  size_t chunk;
  uint64_t expires_at_ms;
  size_t encoded_peer_addr_len;
  size_t original_dcid_len;
  size_t retry_source_cid_len;
  uint8_t expected_mac[AI_QUIC_RETRY_TOKEN_MAC_LEN];

  if (key == NULL || token == NULL ||
      (peer_addr_len > 0u && peer_addr == NULL) ||
      token_len < 1u + 8u + 1u + 1u + 1u + AI_QUIC_RETRY_TOKEN_MAC_LEN) {
    return AI_QUIC_ERROR;
  }

  offset = 0u;
  if (token[offset++] != AI_QUIC_RETRY_TOKEN_FORMAT_VERSION) {
    return AI_QUIC_ERROR;
  }
  if (ai_quic_read_u64(token + offset, token_len - offset, &chunk, &expires_at_ms) !=
      AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }
  offset += chunk;
  if (offset >= token_len) {
    return AI_QUIC_ERROR;
  }
  encoded_peer_addr_len = token[offset++];
  if (encoded_peer_addr_len > AI_QUIC_RETRY_TOKEN_MAX_PEER_ADDR_LEN ||
      token_len - offset < encoded_peer_addr_len + 1u + 1u + AI_QUIC_RETRY_TOKEN_MAC_LEN) {
    return AI_QUIC_ERROR;
  }
  if (peer_addr_len != encoded_peer_addr_len ||
      (encoded_peer_addr_len > 0u &&
       memcmp(token + offset, peer_addr, encoded_peer_addr_len) != 0)) {
    return AI_QUIC_ERROR;
  }
  offset += encoded_peer_addr_len;

  original_dcid_len = token[offset++];
  if (original_dcid_len == 0u || original_dcid_len > AI_QUIC_MAX_CID_LEN ||
      token_len - offset < original_dcid_len + 1u + AI_QUIC_RETRY_TOKEN_MAC_LEN) {
    return AI_QUIC_ERROR;
  }
  if (metadata != NULL &&
      !ai_quic_cid_from_bytes(
          &metadata->original_destination_cid, token + offset, original_dcid_len)) {
    return AI_QUIC_ERROR;
  }
  offset += original_dcid_len;

  retry_source_cid_len = token[offset++];
  if (retry_source_cid_len == 0u || retry_source_cid_len > AI_QUIC_MAX_CID_LEN ||
      token_len - offset != retry_source_cid_len + AI_QUIC_RETRY_TOKEN_MAC_LEN) {
    return AI_QUIC_ERROR;
  }
  if (metadata != NULL &&
      !ai_quic_cid_from_bytes(
          &metadata->retry_source_cid, token + offset, retry_source_cid_len)) {
    return AI_QUIC_ERROR;
  }
  if (packet_dcid != NULL &&
      (packet_dcid->len != retry_source_cid_len ||
       memcmp(packet_dcid->bytes, token + offset, retry_source_cid_len) != 0)) {
    return AI_QUIC_ERROR;
  }
  offset += retry_source_cid_len;

  if (ai_quic_retry_token_mac(key, token, offset, expected_mac) != AI_QUIC_OK ||
      memcmp(expected_mac, token + offset, AI_QUIC_RETRY_TOKEN_MAC_LEN) != 0) {
    return AI_QUIC_ERROR;
  }
  if (now_ms > expires_at_ms) {
    return AI_QUIC_ERROR;
  }

  if (metadata != NULL) {
    metadata->expires_at_ms = expires_at_ms;
  }
  return AI_QUIC_OK;
}
