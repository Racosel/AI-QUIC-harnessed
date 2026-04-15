#include "common_internal.h"

#include <sys/time.h>

uint64_t ai_quic_now_ms(void) {
  struct timeval tv;

  if (gettimeofday(&tv, NULL) != 0) {
    return 0u;
  }

  return ((uint64_t)tv.tv_sec * 1000u) + ((uint64_t)tv.tv_usec / 1000u);
}
