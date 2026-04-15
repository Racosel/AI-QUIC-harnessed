FROM debian:bookworm-slim AS ai_quic_builder

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      bash \
      build-essential \
      ca-certificates \
      cmake \
      golang-go \
      nasm \
      ninja-build \
      perl && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY ai-quic /src/ai-quic

RUN cmake -S /src/ai-quic -B /tmp/ai-quic-build -G Ninja -DCMAKE_BUILD_TYPE=Debug && \
    cmake --build /tmp/ai-quic-build --target ai_quic_demo_server ai_quic_demo_client ai_quic_interop_vn_test

FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      bash \
      ca-certificates \
      ethtool \
      iproute2 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /opt/ai-quic

COPY --from=ai_quic_builder /tmp/ai-quic-build/bin/ai_quic_demo_server /opt/ai-quic/bin/ai_quic_demo_server
COPY --from=ai_quic_builder /tmp/ai-quic-build/bin/ai_quic_demo_client /opt/ai-quic/bin/ai_quic_demo_client
COPY --from=ai_quic_builder /tmp/ai-quic-build/bin/ai_quic_interop_vn_test /opt/ai-quic/bin/ai_quic_interop_vn_test
COPY --from=ai_quic_builder /src/ai-quic/interop/run_endpoint.sh /opt/ai-quic/bin/run_endpoint.sh
COPY --from=ai_quic_builder /src/ai-quic/interop/setup.sh /opt/ai-quic/bin/setup.sh
COPY --from=ai_quic_builder /src/ai-quic/interop/testcase_dispatch.sh /opt/ai-quic/bin/testcase_dispatch.sh
COPY --from=ai_quic_builder /src/ai-quic/interop/lib /opt/ai-quic/bin/lib

RUN chmod +x /opt/ai-quic/bin/ai_quic_demo_server \
    /opt/ai-quic/bin/ai_quic_demo_client \
    /opt/ai-quic/bin/ai_quic_interop_vn_test \
    /opt/ai-quic/bin/run_endpoint.sh \
    /opt/ai-quic/bin/setup.sh \
    /opt/ai-quic/bin/testcase_dispatch.sh

ENTRYPOINT ["/opt/ai-quic/bin/run_endpoint.sh"]
