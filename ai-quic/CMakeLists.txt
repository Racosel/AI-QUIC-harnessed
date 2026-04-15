cmake_minimum_required(VERSION 3.22)

project(ai_quic LANGUAGES C CXX)

add_compile_definitions(_POSIX_C_SOURCE=200809L)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(AI_QUIC_BUILD_TESTS "Build AI-QUIC tests." ON)

set(BUILD_TESTING OFF CACHE BOOL "Disable bundled dependency tests." FORCE)
add_subdirectory(boringssl EXCLUDE_FROM_ALL)

set(AI_QUIC_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(AI_QUIC_COMMON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/common")
set(AI_QUIC_TRANSPORT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/transport")
set(AI_QUIC_TLS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/tls")
set(AI_QUIC_DEMO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/demo/common")
set(AI_QUIC_TEST_SUPPORT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/support")

add_library(
  ai_quic_common
  STATIC
  src/common/buffer.c
  src/common/cid.c
  src/common/fs.c
  src/common/log.c
  src/common/path.c
  src/common/qlog.c
  src/common/random.c
  src/common/result.c
  src/common/time.c
)

target_include_directories(
  ai_quic_common
  PUBLIC
    "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
  PRIVATE
    "${AI_QUIC_COMMON_INCLUDE_DIR}"
)

target_compile_options(
  ai_quic_common
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(
  ai_quic_common
  PUBLIC
    crypto
)

add_library(
  ai_quic_tls
  STATIC
  src/tls/initial_secret.c
  src/tls/key_schedule.c
  src/tls/keyset.c
  src/tls/tls_ctx.c
  src/tls/tls_events.c
  src/tls/tls_session.c
)

target_include_directories(
  ai_quic_tls
  PUBLIC
    "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
  PRIVATE
    "${AI_QUIC_TLS_INCLUDE_DIR}"
    "${AI_QUIC_COMMON_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/boringssl/include"
)

target_link_libraries(
  ai_quic_tls
  PUBLIC
    ai_quic_common
    ssl
    crypto
)

target_compile_options(
  ai_quic_tls
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_library(
  ai_quic_transport
  STATIC
  src/transport/ack.c
  src/transport/conn.c
  src/transport/conn_io.c
  src/transport/conn_state.c
  src/transport/conn_state_helpers.c
  src/transport/crypto_stream.c
  src/transport/dispatcher.c
  src/transport/endpoint.c
  src/transport/frame_codec.c
  src/transport/invariant.c
  src/transport/loss.c
  src/transport/packet_codec.c
  src/transport/pn_space.c
  src/transport/stream.c
  src/transport/timer.c
  src/transport/transport_params.c
  src/transport/version_negotiation.c
)

target_include_directories(
  ai_quic_transport
  PUBLIC
    "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
  PRIVATE
    "${AI_QUIC_COMMON_INCLUDE_DIR}"
    "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
    "${AI_QUIC_TLS_INCLUDE_DIR}"
)

target_link_libraries(
  ai_quic_transport
  PUBLIC
    ai_quic_common
    ai_quic_tls
)

target_compile_options(
  ai_quic_transport
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(
  ai_quic_demo_server
  demo/common/demo_cli.c
  demo/http09/http09_client.c
  demo/http09/self_check.c
  demo/http09/http09_server.c
  demo/server_main.c
)

target_include_directories(
  ai_quic_demo_server
  PRIVATE
    "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
    "${AI_QUIC_COMMON_INCLUDE_DIR}"
    "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
    "${AI_QUIC_DEMO_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/demo"
)

target_link_libraries(
  ai_quic_demo_server
  PRIVATE
    ai_quic_transport
    ai_quic_tls
    ai_quic_common
)

set_property(TARGET ai_quic_demo_server PROPERTY LINKER_LANGUAGE CXX)

add_executable(
  ai_quic_demo_client
  demo/common/demo_cli.c
  demo/http09/http09_server.c
  demo/http09/self_check.c
  demo/http09/http09_client.c
  demo/client_main.c
)

target_include_directories(
  ai_quic_demo_client
  PRIVATE
    "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
    "${AI_QUIC_COMMON_INCLUDE_DIR}"
    "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
    "${AI_QUIC_DEMO_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/demo"
)

target_link_libraries(
  ai_quic_demo_client
  PRIVATE
    ai_quic_transport
    ai_quic_tls
    ai_quic_common
)

set_property(TARGET ai_quic_demo_client PROPERTY LINKER_LANGUAGE CXX)

if(AI_QUIC_BUILD_TESTS)
  enable_testing()

  add_library(
    ai_quic_test_support
    STATIC
    tests/support/test_support.c
  )

  target_include_directories(
    ai_quic_test_support
    PUBLIC
      "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
      "${AI_QUIC_TEST_SUPPORT_INCLUDE_DIR}"
    PRIVATE
      "${AI_QUIC_COMMON_INCLUDE_DIR}"
      "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
  )

  target_link_libraries(
    ai_quic_test_support
    PUBLIC
      ai_quic_transport
      ai_quic_tls
      ai_quic_common
  )

  add_executable(
    ai_quic_unit_test
    tests/unittest/test_unit.c
  )

  target_include_directories(
    ai_quic_unit_test
    PRIVATE
      "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
      "${AI_QUIC_COMMON_INCLUDE_DIR}"
      "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
      "${AI_QUIC_TLS_INCLUDE_DIR}"
      "${AI_QUIC_TEST_SUPPORT_INCLUDE_DIR}"
  )

  target_link_libraries(
    ai_quic_unit_test
    PRIVATE
      ai_quic_test_support
      ai_quic_transport
      ai_quic_tls
      ai_quic_common
  )

  set_property(TARGET ai_quic_unit_test PROPERTY LINKER_LANGUAGE CXX)
  add_test(NAME ai_quic_unit_test COMMAND ai_quic_unit_test)

  add_executable(
    ai_quic_integration_test
    tests/integration/test_handshake.c
  )

  target_include_directories(
    ai_quic_integration_test
    PRIVATE
      "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
      "${AI_QUIC_COMMON_INCLUDE_DIR}"
      "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
      "${AI_QUIC_TEST_SUPPORT_INCLUDE_DIR}"
  )

  target_link_libraries(
    ai_quic_integration_test
    PRIVATE
      ai_quic_test_support
      ai_quic_transport
      ai_quic_tls
      ai_quic_common
  )

  set_property(TARGET ai_quic_integration_test PROPERTY LINKER_LANGUAGE CXX)
  add_test(NAME ai_quic_integration_test COMMAND ai_quic_integration_test)

  add_executable(
    ai_quic_interop_vn_test
    tests/interop/test_versionnegotiation.c
  )

  target_include_directories(
    ai_quic_interop_vn_test
    PRIVATE
      "${AI_QUIC_PUBLIC_INCLUDE_DIR}"
      "${AI_QUIC_COMMON_INCLUDE_DIR}"
      "${AI_QUIC_TRANSPORT_INCLUDE_DIR}"
      "${AI_QUIC_TEST_SUPPORT_INCLUDE_DIR}"
  )

  target_link_libraries(
    ai_quic_interop_vn_test
    PRIVATE
      ai_quic_test_support
      ai_quic_transport
      ai_quic_tls
      ai_quic_common
  )

  set_property(TARGET ai_quic_interop_vn_test PROPERTY LINKER_LANGUAGE CXX)
  add_test(NAME ai_quic_interop_vn_test COMMAND ai_quic_interop_vn_test)

  add_test(
    NAME ai_quic_interop_run_endpoint_test
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/test_run_endpoint.sh"
  )
  add_test(
    NAME ai_quic_interop_local_handshake_test
    COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/test_local_handshake.sh"
  )
endif()
