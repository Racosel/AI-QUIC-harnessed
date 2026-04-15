#!/usr/bin/env bash
set -eu

ai_quic_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ai_quic_run_script_dir="$(cd "${ai_quic_script_dir}/.." && pwd)"

ai_quic_detect_bin_dir() {
  if [ -x "${ai_quic_run_script_dir}/ai_quic_demo_server" ]; then
    printf '%s\n' "${ai_quic_run_script_dir}"
    return 0
  fi
  printf '%s\n' "${AI_QUIC_BIN_DIR:-${PWD}/ai-quic/build/bin}"
}

ai_quic_prepare_logs() {
  mkdir -p "${AI_QUIC_LOG_ROOT}"
  mkdir -p "${AI_QUIC_QLOG_DIR}"
  : > "${AI_QUIC_LOG_FILE}"
  : > "${AI_QUIC_KEYLOG_FILE}"
}

ai_quic_log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S %z')" "$*" >> "${AI_QUIC_LOG_FILE}"
}
