.PHONY: help build test-unit test-integration ai-quic-interop-image quic-demo-server quic-demo-client interop-ai-server interop-ai-client interop-handshake-server interop-handshake-client interop-run interop-handshake-smoke interop-versionnegotiation interop-logs clean clean-test-logs

DEFAULT_SERVER := ai-quic
DEFAULT_CLIENT := ai-quic
DEFAULT_TESTCASE := handshake
AI_QUIC_DIR := ai-quic
AI_QUIC_BUILD_DIR := $(AI_QUIC_DIR)/build
AI_QUIC_IMAGE := ai-quic-interop:latest
AI_QUIC_INTEROP_TESTCASE ?= handshake
AI_QUIC_CMAKE_BUILD_TYPE ?= Debug
INTEROP_RUNNER_DIR := quic-interop-runner
INTEROP_LOG_ROOT := artifacts/interop
INTEROP_DEBUG ?= 1

comma := ,

.DEFAULT_GOAL := help

help:
	@echo "Available targets:"
	@echo "  make build                               # configure and build ai-quic skeleton"
	@echo "  make test-unit                           # run local unit tests"
	@echo "  make test-integration                    # run local integration tests"
	@echo "  make ai-quic-interop-image               # build ai-quic-interop:latest docker image"
	@echo "  make quic-demo-server                    # run ai_quic_demo_server --help"
	@echo "  make quic-demo-client                    # run ai_quic_demo_client --help"
	@echo "  make interop-ai-server                   # ai-quic server vs xquic client"
	@echo "  make interop-ai-client                   # xquic server vs ai-quic client"
	@echo "  make interop-handshake-server            # alias of interop-ai-server for handshake stage"
	@echo "  make interop-handshake-client            # alias of interop-ai-client for handshake stage"
	@echo "  make interop-handshake-smoke             # local ai-quic <-> ai-quic handshake smoke test"
	@echo "  make interop-versionnegotiation          # local version negotiation smoke test"
	@echo "  make interop-run SERVER=x CLIENT=y TESTCASE=z [INTEROP_DEBUG=1]"
	@echo "  make interop-logs [CASE=keyword]          # list saved interop log directories"
	@echo "  make clean                                # remove interop test logs in one step"

build:
	@cmake -S "$(AI_QUIC_DIR)" -B "$(AI_QUIC_BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(AI_QUIC_CMAKE_BUILD_TYPE)"
	@cmake --build "$(AI_QUIC_BUILD_DIR)"

test-unit: build
	@ctest --test-dir "$(AI_QUIC_BUILD_DIR)" --output-on-failure -R '^ai_quic_unit_test$$'

test-integration: build
	@ctest --test-dir "$(AI_QUIC_BUILD_DIR)" --output-on-failure -R 'ai_quic_integration_test|ai_quic_interop_run_endpoint_test|ai_quic_interop_local_handshake_test|ai_quic_interop_vn_test'

ai-quic-interop-image:
	@docker build -f "$(AI_QUIC_DIR)/interop/Dockerfile" -t "$(AI_QUIC_IMAGE)" .

quic-demo-server: build
	@"$(AI_QUIC_BUILD_DIR)/bin/ai_quic_demo_server" --help

quic-demo-client: build
	@"$(AI_QUIC_BUILD_DIR)/bin/ai_quic_demo_client" --help

interop-ai-server: ai-quic-interop-image
	@$(MAKE) interop-run SERVER=ai-quic CLIENT=xquic TESTCASE="$(AI_QUIC_INTEROP_TESTCASE)" INTEROP_DEBUG=$(INTEROP_DEBUG)

interop-ai-client: ai-quic-interop-image
	@$(MAKE) interop-run SERVER=xquic CLIENT=ai-quic TESTCASE="$(AI_QUIC_INTEROP_TESTCASE)" INTEROP_DEBUG=$(INTEROP_DEBUG)

interop-handshake-server: interop-ai-server

interop-handshake-client: interop-ai-client

interop-handshake-smoke: build
	@/bin/bash "$(AI_QUIC_DIR)/tests/interop/test_local_handshake.sh"

interop-versionnegotiation: build
	@"$(AI_QUIC_BUILD_DIR)/bin/ai_quic_interop_vn_test"

interop-run:
	@test -n "$(SERVER)" || { echo "SERVER is required"; exit 2; }
	@test -n "$(CLIENT)" || { echo "CLIENT is required"; exit 2; }
	@test -n "$(TESTCASE)" || { echo "TESTCASE is required"; exit 2; }
	@mkdir -p "$(INTEROP_LOG_ROOT)"
	@set -e; \
	timestamp="$$(date +%Y-%m-%dT%H-%M-%S)"; \
	slug="$${timestamp}-$(SERVER)-$(CLIENT)-$(subst $(comma),-,$(TESTCASE))"; \
	log_dir="$(INTEROP_LOG_ROOT)/$$slug"; \
	runner_log_dir="$$log_dir/runner"; \
	echo "Running interop: server=$(SERVER) client=$(CLIENT) testcase=$(TESTCASE)"; \
	mkdir -p "$$log_dir"; \
	if (cd "$(INTEROP_RUNNER_DIR)" && python3 run.py $(if $(filter 1 true yes,$(INTEROP_DEBUG)),-d,) -s "$(SERVER)" -c "$(CLIENT)" -t "$(TESTCASE)" -l "../$$runner_log_dir") >"$$log_dir/runner-output.txt" 2>&1; then \
		rc=0; \
	else \
		rc=$$?; \
	fi; \
	cat "$$log_dir/runner-output.txt"; \
	printf "server=%s\nclient=%s\ntestcase=%s\nexit_code=%s\n" "$(SERVER)" "$(CLIENT)" "$(TESTCASE)" "$$rc" > "$$log_dir/run-meta.txt"; \
	rm -f "$(INTEROP_LOG_ROOT)/latest"; \
	ln -s "$$slug" "$(INTEROP_LOG_ROOT)/latest"; \
	echo "Saved logs to $$log_dir"; \
	test "$$rc" -eq 0

interop-logs:
	@if [ ! -d "$(INTEROP_LOG_ROOT)" ]; then \
		echo "No interop logs found under $(INTEROP_LOG_ROOT)"; \
	elif [ -n "$(CASE)" ]; then \
		find "$(INTEROP_LOG_ROOT)" -mindepth 1 -maxdepth 1 -type d ! -name latest -name "*$(CASE)*" | sort; \
	else \
		find "$(INTEROP_LOG_ROOT)" -mindepth 1 -maxdepth 1 -type d ! -name latest | sort; \
	fi

clean: clean-test-logs

clean-test-logs:
	@rm -rf "$(INTEROP_LOG_ROOT)"
	@rm -rf "$(INTEROP_RUNNER_DIR)"/logs_*
	@rm -rf "$(AI_QUIC_BUILD_DIR)"
	@echo "Removed interop test logs from $(INTEROP_LOG_ROOT), $(INTEROP_RUNNER_DIR)/logs_* and $(AI_QUIC_BUILD_DIR)"
