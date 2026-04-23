FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      bash \
      ca-certificates \
      ethtool \
      iproute2 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /opt/ai-quic

COPY ai-quic/build/bin/ai_quic_demo_server /opt/ai-quic/bin/ai_quic_demo_server
COPY ai-quic/build/bin/ai_quic_demo_client /opt/ai-quic/bin/ai_quic_demo_client
COPY ai-quic/build/bin/ai_quic_interop_vn_test /opt/ai-quic/bin/ai_quic_interop_vn_test
COPY ai-quic/interop/run_endpoint.sh /opt/ai-quic/bin/run_endpoint.sh
COPY ai-quic/interop/setup.sh /opt/ai-quic/bin/setup.sh
COPY ai-quic/interop/testcase_dispatch.sh /opt/ai-quic/bin/testcase_dispatch.sh
COPY ai-quic/interop/lib /opt/ai-quic/bin/lib

RUN chmod +x /opt/ai-quic/bin/ai_quic_demo_server \
    /opt/ai-quic/bin/ai_quic_demo_client \
    /opt/ai-quic/bin/ai_quic_interop_vn_test \
    /opt/ai-quic/bin/run_endpoint.sh \
    /opt/ai-quic/bin/setup.sh \
    /opt/ai-quic/bin/testcase_dispatch.sh

ENTRYPOINT ["/opt/ai-quic/bin/run_endpoint.sh"]
