#!/usr/bin/env bash
set -eu

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
bin_dir="${repo_root}/ai-quic/build/bin"
tmp_root="$(mktemp -d /tmp/ai_quic_local_handshake.XXXXXX)"
www_dir="${tmp_root}/www"
downloads_dir="${tmp_root}/downloads"
logs_dir="${tmp_root}/logs"
qlog_dir="${tmp_root}/qlog"
mkdir -p "${www_dir}" "${downloads_dir}" "${logs_dir}" "${qlog_dir}"
printf 'local-handshake-data' > "${www_dir}/test.txt"

"${bin_dir}/ai_quic_demo_client" \
  --self-check \
  --log-file "${logs_dir}/self-check.log" \
  --qlog-dir "${qlog_dir}" \
  --ssl-keylog-file "${logs_dir}/keys.log" \
  --cert-dir "${tmp_root}/certs" \
  --www "${www_dir}" \
  --downloads "${downloads_dir}" \
  --testcase handshake
cmp "${www_dir}/test.txt" "${downloads_dir}/test.txt"
