set(AI_QUIC_CORE_SOURCES
    common/ai_quic_version.c
)

add_library(ai_quic_core STATIC ${AI_QUIC_CORE_SOURCES})

target_include_directories(ai_quic_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

