#!/usr/bin/env bash
set -eu

echo "Setting up routes..."

if command -v ethtool >/dev/null 2>&1; then
  ethtool -K eth0 tx off || true
fi

ipv4_addr="$(hostname -I | awk '{print $1}')"
if [ -n "${ipv4_addr}" ]; then
  ipv4_gateway="${ipv4_addr%.*}.2"
  ipv4_local_net="${ipv4_addr%.*}.0/24"
  echo "Endpoint's IPv4 address is ${ipv4_addr}"
  ip route replace 193.167.0.0/16 via "${ipv4_gateway}" dev eth0
  ip route del "${ipv4_local_net}" dev eth0 2>/dev/null || true
fi

ipv6_addr="$(hostname -I | awk '{print $2}')"
if [ -n "${ipv6_addr}" ]; then
  ipv6_gateway="${ipv6_addr%:*}:2"
  ipv6_local_net="${ipv6_addr%:*}::/64"
  echo "Endpoint's IPv6 address is ${ipv6_addr}"
  ip -6 route replace fd00:cafe:cafe::/48 via "${ipv6_gateway}" dev eth0
  ip -6 route del "${ipv6_local_net}" dev eth0 2>/dev/null || true
fi

mkdir -p /logs/qlog
