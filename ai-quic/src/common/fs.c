#include "ai_quic/fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static char *ai_quic_fs_strdup(const char *text) {
  char *copy;
  size_t len;

  if (text == NULL) {
    return NULL;
  }

  len = strlen(text);
  copy = (char *)malloc(len + 1u);
  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, text, len + 1u);
  return copy;
}

static ai_quic_result_t ai_quic_fs_mkdir_single(const char *path) {
  if (path == NULL || path[0] == '\0') {
    return AI_QUIC_ERROR;
  }

  if (mkdir(path, 0755) == 0 || errno == EEXIST) {
    return AI_QUIC_OK;
  }

  return AI_QUIC_ERROR;
}

static ai_quic_result_t ai_quic_fs_prepare_parent(const char *path) {
  char *copy;
  char *slash;
  ai_quic_result_t result;

  copy = ai_quic_fs_strdup(path);
  if (copy == NULL) {
    return AI_QUIC_ERROR;
  }

  slash = strrchr(copy, '/');
  if (slash == NULL) {
    free(copy);
    return AI_QUIC_OK;
  }

  if (slash == copy) {
    free(copy);
    return AI_QUIC_OK;
  }

  *slash = '\0';
  result = ai_quic_fs_ensure_dir(copy);
  free(copy);
  return result;
}

ai_quic_result_t ai_quic_fs_ensure_dir(const char *path) {
  char *copy;
  char *cursor;
  ai_quic_result_t result;

  if (path == NULL || path[0] == '\0') {
    return AI_QUIC_ERROR;
  }

  copy = ai_quic_fs_strdup(path);
  if (copy == NULL) {
    return AI_QUIC_ERROR;
  }

  result = AI_QUIC_OK;
  for (cursor = copy + 1; *cursor != '\0'; ++cursor) {
    if (*cursor != '/') {
      continue;
    }

    *cursor = '\0';
    if (copy[0] != '\0') {
      result = ai_quic_fs_mkdir_single(copy);
      if (result != AI_QUIC_OK) {
        free(copy);
        return result;
      }
    }
    *cursor = '/';
  }

  result = ai_quic_fs_mkdir_single(copy);
  free(copy);
  return result;
}

ai_quic_result_t ai_quic_fs_write_text_file(const char *path, const char *text) {
  FILE *file;

  if (path == NULL || path[0] == '\0') {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_fs_prepare_parent(path) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  file = fopen(path, "w");
  if (file == NULL) {
    return AI_QUIC_ERROR;
  }

  if (text != NULL && text[0] != '\0' && fputs(text, file) == EOF) {
    fclose(file);
    return AI_QUIC_ERROR;
  }

  if (fclose(file) != 0) {
    return AI_QUIC_ERROR;
  }

  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_fs_touch_file(const char *path, const char *text) {
  return ai_quic_fs_write_text_file(path, text);
}

ai_quic_result_t ai_quic_fs_read_binary_file(const char *path,
                                             uint8_t **data,
                                             size_t *data_len) {
  FILE *file;
  long length;
  uint8_t *buffer;

  if (path == NULL || data == NULL || data_len == NULL) {
    return AI_QUIC_ERROR;
  }

  *data = NULL;
  *data_len = 0u;

  file = fopen(path, "rb");
  if (file == NULL) {
    return AI_QUIC_ERROR;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return AI_QUIC_ERROR;
  }

  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return AI_QUIC_ERROR;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return AI_QUIC_ERROR;
  }

  buffer = (uint8_t *)malloc((size_t)length);
  if (buffer == NULL && length > 0) {
    fclose(file);
    return AI_QUIC_ERROR;
  }

  if (length > 0 &&
      fread(buffer, 1u, (size_t)length, file) != (size_t)length) {
    free(buffer);
    fclose(file);
    return AI_QUIC_ERROR;
  }

  if (fclose(file) != 0) {
    free(buffer);
    return AI_QUIC_ERROR;
  }

  *data = buffer;
  *data_len = (size_t)length;
  return AI_QUIC_OK;
}

ai_quic_result_t ai_quic_fs_write_binary_file(const char *path,
                                              const uint8_t *data,
                                              size_t data_len) {
  FILE *file;

  if (path == NULL) {
    return AI_QUIC_ERROR;
  }

  if (ai_quic_fs_prepare_parent(path) != AI_QUIC_OK) {
    return AI_QUIC_ERROR;
  }

  file = fopen(path, "wb");
  if (file == NULL) {
    return AI_QUIC_ERROR;
  }

  if (data_len > 0u &&
      fwrite(data, 1u, data_len, file) != data_len) {
    fclose(file);
    return AI_QUIC_ERROR;
  }

  if (fclose(file) != 0) {
    return AI_QUIC_ERROR;
  }

  return AI_QUIC_OK;
}
