#!/usr/bin/env bash
set -eu

ai_quic_role="${ROLE:-unknown}"
ai_quic_testcase="${TESTCASE:-}"
ai_quic_requests="${REQUESTS:-}"
AI_QUIC_LOG_ROOT="${AI_QUIC_LOG_ROOT:-/logs}"
AI_QUIC_QLOG_DIR="${QLOGDIR:-${AI_QUIC_LOG_ROOT}/qlog}"
AI_QUIC_LOG_FILE="${AI_QUIC_LOG_ROOT}/${ai_quic_role}.log"
AI_QUIC_KEYLOG_FILE="${SSLKEYLOGFILE:-${AI_QUIC_LOG_ROOT}/keys.log}"
AI_QUIC_CERT_DIR="${AI_QUIC_CERT_DIR:-/certs}"
AI_QUIC_WWW_DIR="${AI_QUIC_WWW_DIR:-/www}"
AI_QUIC_DOWNLOADS_DIR="${AI_QUIC_DOWNLOADS_DIR:-/downloads}"
AI_QUIC_PORT="${AI_QUIC_PORT:-443}"
AI_QUIC_BIND_HOST="${AI_QUIC_BIND_HOST:-::}"
AI_QUIC_CLIENT_START_DELAY="${AI_QUIC_CLIENT_START_DELAY:-1}"

ai_quic_self_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "${ai_quic_self_dir}/lib/common.sh"

if [ -z "${ai_quic_testcase}" ]; then
  exit 1
fi

if ! "${ai_quic_self_dir}/testcase_dispatch.sh" "${ai_quic_testcase}"; then
  exit 127
fi

AI_QUIC_BIN_DIR="$(ai_quic_detect_bin_dir)"
ai_quic_prepare_logs

if [ -x "${ai_quic_self_dir}/setup.sh" ]; then
  "${ai_quic_self_dir}/setup.sh" >> "${AI_QUIC_LOG_FILE}" 2>&1
fi

ai_quic_log "role=${ai_quic_role}"
ai_quic_log "testcase=${ai_quic_testcase}"
ai_quic_log "requests=${ai_quic_requests:-"(unset)"}"
ai_quic_log "bin_dir=${AI_QUIC_BIN_DIR}"
ai_quic_log "log_root=${AI_QUIC_LOG_ROOT}"
ai_quic_log "qlog_dir=${AI_QUIC_QLOG_DIR}"
ai_quic_log "keylog=${AI_QUIC_KEYLOG_FILE}"
ai_quic_log "cert_dir=${AI_QUIC_CERT_DIR}"
ai_quic_log "www_dir=${AI_QUIC_WWW_DIR}"
ai_quic_log "downloads_dir=${AI_QUIC_DOWNLOADS_DIR}"
ai_quic_log "bind_host=${AI_QUIC_BIND_HOST}"
ai_quic_log "port=${AI_QUIC_PORT}"
if command -v ip >/dev/null 2>&1; then
  {
    printf 'route_ipv4_begin\n'
    ip route
    printf 'route_ipv4_end\n'
    printf 'route_ipv6_begin\n'
    ip -6 route
    printf 'route_ipv6_end\n'
  } >> "${AI_QUIC_LOG_FILE}" 2>&1 || true
fi

case "${ai_quic_role}" in
  server)
    ai_quic_binary_path="${AI_QUIC_BIN_DIR}/ai_quic_demo_server"
    ;;
  client)
    ai_quic_binary_path="${AI_QUIC_BIN_DIR}/ai_quic_demo_client"
    if [ "${AI_QUIC_CLIENT_START_DELAY}" != "0" ]; then
      ai_quic_log "client_start_delay_seconds=${AI_QUIC_CLIENT_START_DELAY}"
      sleep "${AI_QUIC_CLIENT_START_DELAY}"
    fi
    ;;
  *)
    exit 1
    ;;
esac

if [ ! -x "${ai_quic_binary_path}" ]; then
  ai_quic_log "binary_missing=${ai_quic_binary_path}"
  exit 1
fi

if [ "${ai_quic_testcase}" = "versionnegotiation" ]; then
  ai_quic_log "executing_version_negotiation_test=${AI_QUIC_BIN_DIR}/ai_quic_interop_vn_test"
  "${AI_QUIC_BIN_DIR}/ai_quic_interop_vn_test" >> "${AI_QUIC_LOG_FILE}" 2>&1
  exit $?
fi

ai_quic_log "executing=${ai_quic_binary_path}"
if command -v getent >/dev/null 2>&1; then
  getent hosts server4 >> "${AI_QUIC_LOG_FILE}" 2>&1 || true
  getent hosts server6 >> "${AI_QUIC_LOG_FILE}" 2>&1 || true
fi

rc=0
stdbuf -oL -eL "${ai_quic_binary_path}" \
  --run \
  --log-file "${AI_QUIC_LOG_FILE}" \
  --qlog-dir "${AI_QUIC_QLOG_DIR}/${ai_quic_role}.qlog" \
  --ssl-keylog-file "${AI_QUIC_KEYLOG_FILE}" \
  --cert-dir "${AI_QUIC_CERT_DIR}" \
  --www "${AI_QUIC_WWW_DIR}" \
  --downloads "${AI_QUIC_DOWNLOADS_DIR}" \
  --requests "${ai_quic_requests}" \
  --testcase "${ai_quic_testcase}" \
  --bind-host "${AI_QUIC_BIND_HOST}" \
  --port "${AI_QUIC_PORT}" >> "${AI_QUIC_LOG_FILE}" 2>&1 || rc=$?
ai_quic_log "exit_code=${rc}"
exit "${rc}"
