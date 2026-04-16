.PHONY: help build test-unit test-integration test-interop-handshake test-interop-transfer ai-quic-interop-image quic-demo-server quic-demo-client interop-ai-server interop-ai-client interop-handshake-server interop-handshake-client interop-transfer-server interop-transfer-client interop-repeat interop-run interop-handshake-smoke interop-versionnegotiation interop-logs clean clean-test-logs

AI_QUIC_DIR := ai-quic
AI_QUIC_BUILD_DIR := $(AI_QUIC_DIR)/build
AI_QUIC_IMAGE := ai-quic-interop:latest
AI_QUIC_CMAKE_BUILD_TYPE ?= Debug
INTEROP_RUNNER_DIR := quic-interop-runner
INTEROP_LOG_ROOT := artifacts/interop
INTEROP_DEBUG ?= 1
INTEROP_TEST_REPEAT ?= 1

comma := ,

.DEFAULT_GOAL := help

help:
	@echo "Available targets:"
	@echo "  make build                               # configure and build ai-quic skeleton"
	@echo "  make test-unit                           # run local unit tests"
	@echo "  make test-integration                    # run local integration tests"
	@echo "  make test-interop-handshake              # run handshake interop serially, both directions 5 times each"
	@echo "  make test-interop-transfer               # run transfer interop serially, both directions 5 times each"
	@echo "  make ai-quic-interop-image               # build ai-quic-interop:latest docker image"
	@echo "  make quic-demo-server                    # run ai_quic_demo_server --help"
	@echo "  make quic-demo-client                    # run ai_quic_demo_client --help"
	@echo "  make interop-handshake-server            # ai-quic server vs xquic client, repeated 5 times"
	@echo "  make interop-handshake-client            # xquic server vs ai-quic client, repeated 5 times"
	@echo "  make interop-transfer-server             # ai-quic server vs xquic client for transfer, repeated 5 times"
	@echo "  make interop-transfer-client             # xquic server vs ai-quic client for transfer, repeated 5 times"
	@echo "  make interop-handshake-smoke             # local ai-quic <-> ai-quic handshake smoke test"
	@echo "  make interop-versionnegotiation          # local version negotiation smoke test"
	@echo "  make interop-run SERVER=x CLIENT=y TESTCASE=z [INTEROP_DEBUG=1]"
	@echo "  make interop-logs [CASE=keyword]         # list saved interop log directories"
	@echo "  make clean                               # remove interop test logs in one step"

build:
	@cmake -S "$(AI_QUIC_DIR)" -B "$(AI_QUIC_BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(AI_QUIC_CMAKE_BUILD_TYPE)"
	@cmake --build "$(AI_QUIC_BUILD_DIR)"

test-unit: build
	@ctest --test-dir "$(AI_QUIC_BUILD_DIR)" --output-on-failure -R '^ai_quic_unit_test$$'

test-integration: build
	@ctest --test-dir "$(AI_QUIC_BUILD_DIR)" --output-on-failure -R 'ai_quic_integration_test|ai_quic_interop_run_endpoint_test|ai_quic_interop_local_handshake_test|ai_quic_interop_vn_test'

test-interop-handshake: ai-quic-interop-image
	@$(MAKE) interop-repeat SERVER=ai-quic CLIENT=xquic TESTCASE=handshake INTEROP_DEBUG=$(INTEROP_DEBUG)
	@$(MAKE) interop-repeat SERVER=xquic CLIENT=ai-quic TESTCASE=handshake INTEROP_DEBUG=$(INTEROP_DEBUG)

test-interop-transfer: ai-quic-interop-image
	@$(MAKE) interop-repeat SERVER=ai-quic CLIENT=xquic TESTCASE=transfer INTEROP_DEBUG=$(INTEROP_DEBUG)
	@$(MAKE) interop-repeat SERVER=xquic CLIENT=ai-quic TESTCASE=transfer INTEROP_DEBUG=$(INTEROP_DEBUG)

ai-quic-interop-image:
	@docker build -f "$(AI_QUIC_DIR)/interop/Dockerfile" -t "$(AI_QUIC_IMAGE)" .

quic-demo-server: build
	@"$(AI_QUIC_BUILD_DIR)/bin/ai_quic_demo_server" --help

quic-demo-client: build
	@"$(AI_QUIC_BUILD_DIR)/bin/ai_quic_demo_client" --help

interop-handshake-server: ai-quic-interop-image
	@$(MAKE) interop-repeat SERVER=ai-quic CLIENT=xquic TESTCASE=handshake INTEROP_DEBUG=$(INTEROP_DEBUG)

interop-handshake-client: ai-quic-interop-image
	@$(MAKE) interop-repeat SERVER=xquic CLIENT=ai-quic TESTCASE=handshake INTEROP_DEBUG=$(INTEROP_DEBUG)

interop-transfer-server: ai-quic-interop-image
	@$(MAKE) interop-repeat SERVER=ai-quic CLIENT=xquic TESTCASE=transfer INTEROP_DEBUG=$(INTEROP_DEBUG)

interop-transfer-client: ai-quic-interop-image
	@$(MAKE) interop-repeat SERVER=xquic CLIENT=ai-quic TESTCASE=transfer INTEROP_DEBUG=$(INTEROP_DEBUG)

interop-handshake-smoke: build
	@/bin/bash "$(AI_QUIC_DIR)/tests/interop/test_local_handshake.sh"

interop-versionnegotiation: build
	@"$(AI_QUIC_BUILD_DIR)/bin/ai_quic_interop_vn_test"

interop-repeat:
	@test -n "$(SERVER)" || { echo "SERVER is required"; exit 2; }
	@test -n "$(CLIENT)" || { echo "CLIENT is required"; exit 2; }
	@test -n "$(TESTCASE)" || { echo "TESTCASE is required"; exit 2; }
	@set -e; \
	repeat="$(INTEROP_TEST_REPEAT)"; \
	i=1; \
	while [ "$$i" -le "$$repeat" ]; do \
		echo "[$$i/$$repeat] interop testcase=$(TESTCASE) server=$(SERVER) client=$(CLIENT)"; \
		if ! $(MAKE) interop-run SERVER="$(SERVER)" CLIENT="$(CLIENT)" TESTCASE="$(TESTCASE)" INTEROP_DEBUG=$(INTEROP_DEBUG); then \
			echo "interop failed at iteration $$i: testcase=$(TESTCASE) server=$(SERVER) client=$(CLIENT)"; \
			exit 1; \
		fi; \
		i=$$((i + 1)); \
	done

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
