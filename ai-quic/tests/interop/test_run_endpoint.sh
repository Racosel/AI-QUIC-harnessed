#!/usr/bin/env bash
set -eu

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
bin_dir="${repo_root}/ai-quic/build/bin"
interop_script="${repo_root}/ai-quic/interop/run_endpoint.sh"
dispatch_script="${repo_root}/ai-quic/interop/testcase_dispatch.sh"
tmp_root="$(mktemp -d /tmp/ai_quic_run_endpoint.XXXXXX)"
logs_dir="${tmp_root}/logs"
qlog_dir="${tmp_root}/qlog"
mkdir -p "${logs_dir}" "${qlog_dir}"

"${dispatch_script}" retry

if AI_QUIC_BIN_DIR="${bin_dir}" \
  AI_QUIC_LOG_ROOT="${logs_dir}" \
  AI_QUIC_QLOG_DIR="${qlog_dir}" \
  ROLE=unknown \
  TESTCASE=retry \
  "${interop_script}"; then
  exit 1
else
  rc=$?
fi

test "${rc}" -eq 1

AI_QUIC_BIN_DIR="${bin_dir}" \
AI_QUIC_LOG_ROOT="${logs_dir}" \
AI_QUIC_QLOG_DIR="${qlog_dir}" \
ROLE=server \
TESTCASE=versionnegotiation \
"${interop_script}"
