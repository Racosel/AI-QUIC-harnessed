#include "ai_quic/qlog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_quic/fs.h"

struct ai_quic_qlog_writer {
  FILE *file;
  int has_reference_time;
  uint64_t reference_time_ms;
};

static void ai_quic_qlog_write_json_escaped(FILE *file, const char *value) {
  const unsigned char *cursor;

  if (file == NULL) {
    return;
  }

  fputc('"', file);
  if (value != NULL) {
    cursor = (const unsigned char *)value;
    while (*cursor != '\0') {
      switch (*cursor) {
        case '\\':
        case '"':
          fputc('\\', file);
          fputc((int)*cursor, file);
          break;
        case '\b':
          fputs("\\b", file);
          break;
        case '\f':
          fputs("\\f", file);
          break;
        case '\n':
          fputs("\\n", file);
          break;
        case '\r':
          fputs("\\r", file);
          break;
        case '\t':
          fputs("\\t", file);
          break;
        default:
          if (iscntrl(*cursor)) {
            fprintf(file, "\\u%04x", (unsigned int)*cursor);
          } else {
            fputc((int)*cursor, file);
          }
          break;
      }
      ++cursor;
    }
  }
  fputc('"', file);
}

ai_quic_qlog_writer_t *ai_quic_qlog_writer_create(const char *path,
                                                  const char *title,
                                                  const char *vantage_type) {
  ai_quic_qlog_writer_t *writer;

  if (path == NULL) {
    return NULL;
  }

  if (ai_quic_fs_write_text_file(path, "") != AI_QUIC_OK) {
    return NULL;
  }

  writer = (ai_quic_qlog_writer_t *)calloc(1u, sizeof(*writer));
  if (writer == NULL) {
    return NULL;
  }

  writer->file = fopen(path, "a");
  if (writer->file == NULL) {
    free(writer);
    return NULL;
  }

  writer->has_reference_time = 0;
  writer->reference_time_ms = 0u;

  fputc(0x1e, writer->file);
  fputs("{\"qlog_version\":\"0.3\",\"qlog_format\":\"JSON-SEQ\"", writer->file);
  fputs(",\"trace\":{\"title\":", writer->file);
  ai_quic_qlog_write_json_escaped(writer->file, title != NULL ? title : "ai-quic");
  fputs(",\"vantage_point\":{\"type\":", writer->file);
  ai_quic_qlog_write_json_escaped(writer->file,
                                  vantage_type != NULL ? vantage_type : "unknown");
  fputs(",\"name\":\"ai-quic\"}", writer->file);
  fputs(",\"common_fields\":{\"protocol_type\":[\"QUIC\"],\"time_format\":\"relative\"}",
        writer->file);
  fputc('}', writer->file);
  fputc('}', writer->file);
  fputc('\n', writer->file);
  fflush(writer->file);
  return writer;
}

void ai_quic_qlog_writer_destroy(ai_quic_qlog_writer_t *writer) {
  if (writer == NULL) {
    return;
  }
  if (writer->file != NULL) {
    fclose(writer->file);
  }
  free(writer);
}

void ai_quic_qlog_write_event(ai_quic_qlog_writer_t *writer,
                              uint64_t now_ms,
                              const char *category,
                              const char *event_name,
                              const char *data_json) {
  uint64_t relative_time;

  if (writer == NULL || writer->file == NULL || category == NULL || event_name == NULL) {
    return;
  }

  if (!writer->has_reference_time) {
    writer->has_reference_time = 1;
    writer->reference_time_ms = now_ms;
  }
  relative_time = now_ms - writer->reference_time_ms;

  fputc(0x1e, writer->file);
  fprintf(writer->file, "{\"time\":%llu,\"name\":",
          (unsigned long long)relative_time);
  {
    char combined_name[256];

    snprintf(combined_name,
             sizeof(combined_name),
             "%s:%s",
             category,
             event_name);
    ai_quic_qlog_write_json_escaped(writer->file, combined_name);
  }
  fputs(",\"data\":", writer->file);
  fputs(data_json != NULL ? data_json : "{}", writer->file);
  fputs("}\n", writer->file);
  fflush(writer->file);
}

void ai_quic_qlog_write_key_value(ai_quic_qlog_writer_t *writer,
                                  uint64_t now_ms,
                                  const char *category,
                                  const char *event_name,
                                  const char *detail_key,
                                  const char *detail_value) {
  char data_json[512];
  const char *key;
  const char *value;
  size_t offset;
  const unsigned char *cursor;

  key = detail_key != NULL ? detail_key : "detail";
  value = detail_value != NULL ? detail_value : "";

  offset = 0u;
  offset += (size_t)snprintf(data_json + offset, sizeof(data_json) - offset, "{\"");
  for (cursor = (const unsigned char *)key;
       *cursor != '\0' && offset + 7u < sizeof(data_json);
       ++cursor) {
    if (*cursor == '\\' || *cursor == '"') {
      data_json[offset++] = '\\';
    }
    data_json[offset++] = (char)*cursor;
  }
  offset += (size_t)snprintf(data_json + offset, sizeof(data_json) - offset, "\":\"");
  for (cursor = (const unsigned char *)value;
       *cursor != '\0' && offset + 7u < sizeof(data_json);
       ++cursor) {
    if (*cursor == '\\' || *cursor == '"') {
      data_json[offset++] = '\\';
      data_json[offset++] = (char)*cursor;
    } else if (*cursor == '\n') {
      data_json[offset++] = '\\';
      data_json[offset++] = 'n';
    } else if (*cursor == '\r') {
      data_json[offset++] = '\\';
      data_json[offset++] = 'r';
    } else if (*cursor == '\t') {
      data_json[offset++] = '\\';
      data_json[offset++] = 't';
    } else if (!iscntrl(*cursor)) {
      data_json[offset++] = (char)*cursor;
    }
  }
  if (offset + 3u >= sizeof(data_json)) {
    return;
  }
  data_json[offset++] = '"';
  data_json[offset++] = '}';
  data_json[offset] = '\0';

  ai_quic_qlog_write_event(writer, now_ms, category, event_name, data_json);
}
