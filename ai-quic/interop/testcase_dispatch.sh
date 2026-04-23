#!/usr/bin/env bash
set -eu

ai_quic_testcase="${1:-}"

case "${ai_quic_testcase}" in
  handshake|versionnegotiation|transfer|v2|chacha20|retry)
    exit 0
    ;;
  *)
    exit 127
    ;;
esac
