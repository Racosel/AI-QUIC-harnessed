#ifndef AI_QUIC_QLOG_H
#define AI_QUIC_QLOG_H

#include <stdint.h>

typedef struct ai_quic_qlog_writer ai_quic_qlog_writer_t;

ai_quic_qlog_writer_t *ai_quic_qlog_writer_create(const char *path,
                                                  const char *title,
                                                  const char *vantage_type);
void ai_quic_qlog_writer_destroy(ai_quic_qlog_writer_t *writer);
void ai_quic_qlog_write_event(ai_quic_qlog_writer_t *writer,
                              uint64_t now_ms,
                              const char *category,
                              const char *event_name,
                              const char *data_json);
void ai_quic_qlog_write_key_value(ai_quic_qlog_writer_t *writer,
                                  uint64_t now_ms,
                                  const char *category,
                                  const char *event_name,
                                  const char *detail_key,
                                  const char *detail_value);

#endif
