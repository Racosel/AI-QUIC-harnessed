#ifndef AI_QUIC_FS_H
#define AI_QUIC_FS_H

#include <stddef.h>
#include <stdint.h>

#include "ai_quic/result.h"

ai_quic_result_t ai_quic_fs_ensure_dir(const char *path);
ai_quic_result_t ai_quic_fs_write_text_file(const char *path, const char *text);
ai_quic_result_t ai_quic_fs_touch_file(const char *path, const char *text);
ai_quic_result_t ai_quic_fs_read_binary_file(const char *path,
                                             uint8_t **data,
                                             size_t *data_len);
ai_quic_result_t ai_quic_fs_write_binary_file(const char *path,
                                              const uint8_t *data,
                                              size_t data_len);

#endif
