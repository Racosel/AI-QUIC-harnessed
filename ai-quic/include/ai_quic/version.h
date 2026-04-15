#ifndef AI_QUIC_VERSION_H
#define AI_QUIC_VERSION_H

#include <stdint.h>

typedef uint32_t ai_quic_version_t;

#define AI_QUIC_VERSION_V1 ((ai_quic_version_t)0x00000001u)
#define AI_QUIC_VERSION_V2 ((ai_quic_version_t)0x6b3343cfu)

#endif
