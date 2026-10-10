QNX_SDK ?= /mnt/d/tools/qnx660-sdk
QNX_HOST := $(QNX_SDK)/host/linux/x86
QNX_TARGET := $(QNX_SDK)/target/qnx6
HOST_LIBS ?= /mnt/d/tools/qnx-host-libs
QCC := $(QNX_HOST)/usr/bin/qcc
QNX_READELF := $(QNX_HOST)/usr/bin/ntoarmv7-readelf
QNX_OBJDUMP := $(QNX_HOST)/usr/bin/ntoarmv7-objdump
QNX_STRINGS := $(QNX_HOST)/usr/bin/ntoarmv7-strings
CALLBACK_MATRIX ?= $(CURDIR)/contracts/qr39/observations/callback-api-matrix.csv
PROVIDER_MATRIX ?= $(CURDIR)/contracts/qr39/observations/provider-api-matrix.csv
POINTER_MATRIX ?= $(CURDIR)/contracts/qr39/observations/pointer-api-matrix.csv

BUILD_DIR ?= /tmp/mock-navicore-build
DIST_DIR ?= $(CURDIR)/dist
TARGET := $(DIST_DIR)/libNaviCore.so
OBJECTS := $(BUILD_DIR)/mock_navicore.o $(BUILD_DIR)/mock_api_telemetry.o \
	$(BUILD_DIR)/mock_callback_backend.o \
	$(BUILD_DIR)/mock_cluster_backend.o \
	$(BUILD_DIR)/mock_backend_loader.o \
	$(BUILD_DIR)/mock_pas_callbacks.o \
	$(BUILD_DIR)/mock_gps_backend.o $(BUILD_DIR)/mock_pointer_backend.o \
	$(BUILD_DIR)/mock_scalar_backend.o $(BUILD_DIR)/mock_renderer.o \
	$(BUILD_DIR)/mock_screen_input.o \
	$(BUILD_DIR)/mock_default_backend.o $(BUILD_DIR)/mock_renderer_qnx.o
SMOKE_TARGET := $(DIST_DIR)/mock_navicore_smoke
SMOKE_OBJECT := $(BUILD_DIR)/qnx_dlopen_smoke.o
PLUGIN_TARGET := $(DIST_DIR)/libMockNavBackendReference.so
PLUGIN_OBJECT := $(BUILD_DIR)/reference_backend.o
OUTBOUND_PLUGIN_TARGET := $(DIST_DIR)/libMockNavBackendOutboundTest.so
OUTBOUND_PLUGIN_OBJECT := $(BUILD_DIR)/reference_backend_outbound_test.o
PLUGIN_SMOKE_TARGET := $(DIST_DIR)/mock_nav_backend_plugin_smoke
PLUGIN_SMOKE_OBJECT := $(BUILD_DIR)/qnx_plugin_dlopen_smoke.o
RAW_CAPTURE_TARGET := $(DIST_DIR)/libMockRawCallbackCapture.so
RAW_CAPTURE_OBJECTS := $(BUILD_DIR)/mock_raw_callback_capture.o \
	$(BUILD_DIR)/mock_raw_callback_trampolines_arm.o \
	$(BUILD_DIR)/mock_raw_callback_slots_arm.o \
	$(BUILD_DIR)/mock_raw_callback_slot_table.o

# Keep the host provider composition and renderer seam shared by smoke tests.
HOST_RENDERER_SOURCES := src/renderer/mock_renderer.c src/renderer/mock_screen_input.c \
	src/backend/mock_default_backend.c src/platform/host/mock_renderer_stub.c
HOST_PROVIDER_SOURCES := src/compat/qr39/mock_navicore.c src/runtime/mock_api_telemetry.c \
	src/runtime/mock_callback_backend.c src/runtime/mock_cluster_backend.c \
	src/backend/mock_backend_loader.c src/compat/qr39/mock_pas_callbacks.c src/runtime/mock_gps_backend.c \
	src/runtime/mock_pointer_backend.c src/runtime/mock_scalar_backend.c $(HOST_RENDERER_SOURCES)

PRIVATE_INCLUDE_DIRS := -Isrc/compat/qr39 -Isrc/runtime -Isrc/backend \
	-Isrc/renderer -Isrc/capture
CPPFLAGS := -I$(CURDIR)/include $(PRIVATE_INCLUDE_DIRS)
CFLAGS := -Vgcc_ntoarmv7le -std=c99 -O2 -g -Wall -Wextra -Werror \
	-D_REENTRANT -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections
LDFLAGS := -Vgcc_ntoarmv7le -shared -Wl,-soname,libNaviCore.so \
	-Wl,--gc-sections -Wl,--no-undefined

.PHONY: all verify verify-qnx verify-bench test test-host check-contracts \
	contract-check full-check replay-stock provider-coverage-selftest \
	static-provider-coverage-selftest static-callback-coverage-selftest \
	pas-proxy-coverage-selftest qnx-arm-abi-oracle-selftest \
	arm-snapshot-replay-selftest raw-callback-capture \
	raw-callback-capture-selftest clean

all: $(TARGET) $(SMOKE_TARGET) $(PLUGIN_TARGET) $(OUTBOUND_PLUGIN_TARGET) \
	$(PLUGIN_SMOKE_TARGET)

raw-callback-capture: $(RAW_CAPTURE_TARGET)

$(RAW_CAPTURE_TARGET): $(RAW_CAPTURE_OBJECTS) src/capture/mock_raw_callback_capture.map | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -shared \
		-Wl,-soname,libMockRawCallbackCapture.so \
		-Wl,--version-script=$(CURDIR)/src/capture/mock_raw_callback_capture.map \
		-Wl,--no-undefined -o $@ $(RAW_CAPTURE_OBJECTS)

$(BUILD_DIR)/mock_raw_callback_capture.o: src/capture/mock_raw_callback_capture.c src/capture/mock_raw_callback_capture_internal.h include/mock_raw_callback_capture.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(filter-out -fvisibility=hidden,$(CFLAGS)) -c $< -o $@

$(BUILD_DIR)/mock_raw_callback_trampolines_arm.o: src/capture/mock_raw_callback_trampolines_arm.S Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -fPIC -c $< -o $@

$(BUILD_DIR)/mock_raw_callback_slots_arm.o: src/capture/generated/mock_raw_callback_slots_arm.S Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -fPIC -c $< -o $@

$(BUILD_DIR)/mock_raw_callback_slot_table.o: src/capture/generated/mock_raw_callback_slot_table.c src/capture/mock_raw_callback_capture_internal.h include/mock_raw_callback_capture.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(filter-out -fvisibility=hidden,$(CFLAGS)) -c $< -o $@

$(TARGET): $(OBJECTS) | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(LDFLAGS) -o $@ $^ -lscreen -lEGL -lGLESv2

$(BUILD_DIR)/mock_navicore.o: src/compat/qr39/mock_navicore.c src/compat/qr39/mock_static_provider_slots.def src/backend/mock_backend_loader.h include/mock_navicore.h include/mock_api_telemetry.h include/mock_callback_backend.h include/open_navicore/cluster.h include/mock_gps_backend.h include/mock_pointer_backend.h include/mock_scalar_backend.h include/mock_renderer.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_api_telemetry.o: src/runtime/mock_api_telemetry.c src/runtime/mock_counter.h include/mock_api_telemetry.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_callback_backend.o: src/runtime/mock_callback_backend.c src/runtime/mock_counter.h include/mock_callback_backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_backend_loader.o: src/backend/mock_backend_loader.c src/backend/mock_backend_loader.h src/backend/mock_default_backend.h include/open_navicore/backend_plugin.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_cluster_backend.o: src/runtime/mock_cluster_backend.c include/open_navicore/cluster.h include/mock_callback_backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_pas_callbacks.o: src/compat/qr39/mock_pas_callbacks.c include/mock_pas_callbacks.h include/mock_callback_backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_gps_backend.o: src/runtime/mock_gps_backend.c src/runtime/mock_counter.h include/mock_gps_backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_pointer_backend.o: src/runtime/mock_pointer_backend.c src/runtime/mock_counter.h include/mock_pointer_backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_scalar_backend.o: src/runtime/mock_scalar_backend.c src/runtime/mock_counter.h include/mock_scalar_backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_renderer.o: src/renderer/mock_renderer.c src/renderer/mock_renderer_platform.h src/backend/mock_default_backend.h include/mock_renderer.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_screen_input.o: src/renderer/mock_screen_input.c src/renderer/mock_screen_input.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_default_backend.o: src/backend/mock_default_backend.c src/backend/mock_default_backend.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mock_renderer_qnx.o: src/platform/qnx/mock_renderer_qnx.c src/renderer/mock_renderer_platform.h src/renderer/mock_screen_input.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(SMOKE_TARGET): $(SMOKE_OBJECT) | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -o $@ $^

$(SMOKE_OBJECT): tests/qnx_dlopen_smoke.c include/mock_navicore.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(PLUGIN_TARGET): $(PLUGIN_OBJECT) backends/reference/reference_backend.map | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -shared -Wl,-soname,libMockNavBackendReference.so \
		-Wl,--version-script=$(CURDIR)/backends/reference/reference_backend.map \
		-Wl,--no-undefined -Wl,--gc-sections -o $@ $(PLUGIN_OBJECT) -lGLESv2

$(PLUGIN_OBJECT): backends/reference/reference_backend.c include/open_navicore/backend_plugin.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OUTBOUND_PLUGIN_TARGET): $(OUTBOUND_PLUGIN_OBJECT) backends/reference/reference_backend.map | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -shared \
		-Wl,-soname,libMockNavBackendOutboundTest.so \
		-Wl,--version-script=$(CURDIR)/backends/reference/reference_backend.map \
		-Wl,--no-undefined -Wl,--gc-sections -o $@ \
		$(OUTBOUND_PLUGIN_OBJECT) -lGLESv2

$(OUTBOUND_PLUGIN_OBJECT): backends/reference/reference_backend.c include/open_navicore/backend_plugin.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) \
		-DMOCK_REF_PLUGIN_OUTBOUND_TEST=1 -c $< -o $@

$(PLUGIN_SMOKE_TARGET): $(PLUGIN_SMOKE_OBJECT) | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) -Vgcc_ntoarmv7le -o $@ $^

$(PLUGIN_SMOKE_OBJECT): tests/qnx_plugin_dlopen_smoke.c include/mock_navicore.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

# Self-contained checks available from a public source checkout. These consume
# only committed source, contracts, and evidence; no QNX SDK or stock ELF.
check-contracts: pas-proxy-coverage-selftest
	sh tests/check_plugin_elf.sh --self-test
	python3 tests/check_raw_callback_capture.py
	sh tests/check_callback_coverage.sh $(CALLBACK_MATRIX) contracts/qr39/callbacks/runtime-coverage.csv src/compat/qr39/mock_navicore.c
	python3 tests/check_static_callback_additions.py contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv
	python3 tests/check_static_callback_thunk_ownership.py \
		evidence/qr39/static/callback-thunk-ownership.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv contracts/qr39/callbacks/static-contracts.csv
	python3 tests/check_static_callback_contracts.py \
		contracts/qr39/callbacks/static-contracts.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv include/mock_pas_callbacks.h \
		src/compat/qr39/mock_pas_callbacks.c src/compat/qr39/mock_navicore.c
	python3 tests/check_static_callback_outside_inventory.py \
		evidence/qr39/static/callback-outside-inventory.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv contracts/qr39/callbacks/static-contracts.csv \
		evidence/qr39/static/callback-unresolved-next-actions.csv \
		evidence/qr39/static/callback-receiver-dataflow.csv \
		evidence/qr39/static/callback-arm-slices.csv
	python3 tests/check_static_callback_field_origins.py \
		evidence/qr39/static/callback-field-origins.csv \
		evidence/qr39/static/callback-field-origin-groups.csv \
		evidence/qr39/static/callback-outside-inventory.csv \
		evidence/qr39/static/callback-arm-slices.csv
	python3 tests/check_provider_coverage.py $(PROVIDER_MATRIX) $(POINTER_MATRIX) \
		contracts/qr39/provider/api-coverage.csv src/compat/qr39/mock_navicore.c
	python3 tests/check_static_provider_slots.py contracts/qr39/provider/slot-manifest.csv \
		src/compat/qr39/mock_navicore.c src/compat/qr39/mock_static_provider_slots.def

# Backwards-compatible full contract target. The explicit targets below make
# proprietary SDK and stock-image requirements visible to public contributors.
contract-check: check-contracts qnx-arm-abi-oracle-selftest replay-stock \
	raw-callback-capture-selftest

verify-qnx: $(TARGET) $(SMOKE_TARGET) $(PLUGIN_TARGET) $(OUTBOUND_PLUGIN_TARGET) \
	$(PLUGIN_SMOKE_TARGET) check-contracts qnx-arm-abi-oracle-selftest \
	raw-callback-capture-selftest
	LD_LIBRARY_PATH=$(HOST_LIBS) READELF=$(QNX_READELF) \
		sh tests/check_elf.sh $(TARGET)
	LD_LIBRARY_PATH=$(HOST_LIBS) $(QNX_READELF) -h $(SMOKE_TARGET) | \
		grep -q 'Machine:.*ARM'
	LD_LIBRARY_PATH=$(HOST_LIBS) $(QNX_READELF) -h $(PLUGIN_SMOKE_TARGET) | \
		grep -q 'Machine:.*ARM'
	LD_LIBRARY_PATH=$(HOST_LIBS) OBJDUMP=$(QNX_OBJDUMP) \
		sh tests/check_arm_abi.sh $(BUILD_DIR)/mock_navicore.o \
			$(BUILD_DIR)/mock_pas_callbacks.o
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CPPFLAGS) $(CFLAGS) -c tests/backend_abi.c \
		-o $(BUILD_DIR)/backend_abi.o
	LD_LIBRARY_PATH=$(HOST_LIBS) READELF=$(QNX_READELF) \
		sh tests/check_plugin_elf.sh $(PLUGIN_TARGET)
	LD_LIBRARY_PATH=$(HOST_LIBS) READELF=$(QNX_READELF) \
		sh tests/check_plugin_elf.sh $(OUTBOUND_PLUGIN_TARGET)
	LD_LIBRARY_PATH=$(HOST_LIBS) $(QNX_READELF) -d $(OUTBOUND_PLUGIN_TARGET) | \
		grep -q 'SONAME.*\[libMockNavBackendOutboundTest.so\]'
	LD_LIBRARY_PATH=$(HOST_LIBS) $(QNX_STRINGS) $(OUTBOUND_PLUGIN_TARGET) | \
		grep -Fxq 'outbound_test_trigger render=1'
	! LD_LIBRARY_PATH=$(HOST_LIBS) $(QNX_STRINGS) $(PLUGIN_TARGET) | \
		grep -Fq 'outbound_test_trigger'

verify: verify-qnx replay-stock

full-check: test-host verify

replay-stock: arm-snapshot-replay-selftest

verify-bench:
	@echo "Bench verification is operator-run; see docs/development/validation-tiers.md"

test-host: check-contracts
	mkdir -p $(BUILD_DIR)
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread \
		-DMOCK_NAVICORE_DISABLE_LOG \
		-DMOCK_NAVICORE_TESTING \
		-DMOCK_NAVICORE_RENDERER_STUB \
		$(CPPFLAGS) \
		$(HOST_PROVIDER_SOURCES) \
		tests/smoke.c \
		-ldl -o $(BUILD_DIR)/host-smoke
	$(BUILD_DIR)/host-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread \
		-DMOCK_NAVICORE_DISABLE_LOG \
		-DMOCK_NAVICORE_TESTING \
		-DMOCK_NAVICORE_RENDERER_STUB \
		$(CPPFLAGS) \
		$(HOST_PROVIDER_SOURCES) \
		tests/lifecycle_smoke.c \
		-ldl -o $(BUILD_DIR)/lifecycle-smoke
	$(BUILD_DIR)/lifecycle-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread -DMOCK_NAVICORE_TESTING $(CPPFLAGS) \
		src/runtime/mock_callback_backend.c src/compat/qr39/mock_pas_callbacks.c \
		tests/pas_callbacks_smoke.c -o $(BUILD_DIR)/pas-callbacks-smoke
	$(BUILD_DIR)/pas-callbacks-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread $(CPPFLAGS) src/runtime/mock_cluster_backend.c \
		tests/cluster_backend_smoke.c -o $(BUILD_DIR)/cluster-backend-smoke
	$(BUILD_DIR)/cluster-backend-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		$(CPPFLAGS) src/renderer/mock_screen_input.c tests/screen_input_smoke.c \
		-o $(BUILD_DIR)/screen-input-smoke
	$(BUILD_DIR)/screen-input-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		$(CPPFLAGS) -Itests/fixtures/fake_gles \
		src/backend/mock_default_backend.c tests/default_backend_gl_smoke.c \
		-o $(BUILD_DIR)/default-backend-gl-smoke
	$(BUILD_DIR)/default-backend-gl-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread $(CPPFLAGS) -Itests/fixtures/fake_gles \
		backends/reference/reference_backend.c tests/reference_backend_gl_smoke.c \
		-o $(BUILD_DIR)/reference-backend-gl-smoke
	$(BUILD_DIR)/reference-backend-gl-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread -DMOCK_NAVICORE_TESTING -DMOCK_NAVICORE_RENDERER_STUB \
		$(CPPFLAGS) $(HOST_RENDERER_SOURCES) tests/backend_seam_smoke.c \
		-o $(BUILD_DIR)/backend-seam-smoke
	$(BUILD_DIR)/backend-seam-smoke
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread -DMOCK_NAVICORE_TESTING -DMOCK_NAVICORE_RENDERER_STUB \
		$(CPPFLAGS) $(HOST_RENDERER_SOURCES) tests/backend_runtime_stress.c \
		-o $(BUILD_DIR)/backend-runtime-stress
	$(BUILD_DIR)/backend-runtime-stress
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-pthread \
		-DMOCK_NAVICORE_TRACE_PATH='"/tmp/mock-navicore-host-test.log"' \
		-DMOCK_NAVICORE_DISABLE_SYSLOG \
		-DMOCK_NAVICORE_RENDERER_STUB \
		$(CPPFLAGS) $(HOST_PROVIDER_SOURCES) tests/trace_smoke.c \
		-ldl -o $(BUILD_DIR)/host-trace-smoke
	$(BUILD_DIR)/host-trace-smoke
	CC='$(CC)' sh tests/run_plugin_tests.sh $(BUILD_DIR)

# Preserve the established command while making its portable scope explicit.
test: test-host

provider-coverage-selftest:
	python3 tests/check_provider_coverage.py $(PROVIDER_MATRIX) $(POINTER_MATRIX) \
		contracts/qr39/provider/api-coverage.csv src/compat/qr39/mock_navicore.c --self-test

static-provider-coverage-selftest:
	python3 tests/check_static_provider_slots.py contracts/qr39/provider/slot-manifest.csv \
		src/compat/qr39/mock_navicore.c src/compat/qr39/mock_static_provider_slots.def --self-test

static-callback-coverage-selftest: pas-proxy-coverage-selftest
	python3 tests/check_static_callback_additions.py contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv
	python3 tests/check_static_callback_thunk_ownership.py \
		evidence/qr39/static/callback-thunk-ownership.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv contracts/qr39/callbacks/static-contracts.csv \
		--self-test
	python3 tests/check_static_callback_contracts.py \
		contracts/qr39/callbacks/static-contracts.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv include/mock_pas_callbacks.h \
		src/compat/qr39/mock_pas_callbacks.c src/compat/qr39/mock_navicore.c
	python3 tests/check_static_callback_outside_inventory.py \
		evidence/qr39/static/callback-outside-inventory.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv contracts/qr39/callbacks/static-contracts.csv \
		evidence/qr39/static/callback-unresolved-next-actions.csv \
		evidence/qr39/static/callback-receiver-dataflow.csv \
		evidence/qr39/static/callback-arm-slices.csv \
		--self-test
	python3 tests/check_static_callback_field_origins.py \
		evidence/qr39/static/callback-field-origins.csv \
		evidence/qr39/static/callback-field-origin-groups.csv \
		evidence/qr39/static/callback-outside-inventory.csv \
		evidence/qr39/static/callback-arm-slices.csv \
		--self-test

pas-proxy-coverage-selftest:
	mkdir -p $(BUILD_DIR)
	python3 tools/build_pas_proxy_slot_manifest.py \
		evidence/qr39/static/pas-proxy-vtables.csv $(CALLBACK_MATRIX) \
		contracts/qr39/callbacks/runtime-coverage.csv contracts/qr39/callbacks/dispatch-additions.csv \
		contracts/qr39/callbacks/static-contracts.csv \
		evidence/qr39/cross-version/callback-contracts.csv \
		evidence/qr39/cross-version/concrete-vtable-audit.csv \
		$(BUILD_DIR)/pas-callback-proxy-slot-manifest.generated.csv
	cmp contracts/qr39/callbacks/proxy-slot-manifest.csv \
		$(BUILD_DIR)/pas-callback-proxy-slot-manifest.generated.csv
	python3 tests/check_pas_proxy_slot_manifest.py \
		contracts/qr39/callbacks/proxy-slot-manifest.csv evidence/qr39/static/pas-proxy-vtables.csv \
		$(CALLBACK_MATRIX) contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv \
		contracts/qr39/callbacks/static-contracts.csv \
		evidence/qr39/cross-version/callback-contracts.csv \
		evidence/qr39/cross-version/concrete-vtable-audit.csv \
		include/mock_pas_callbacks.h src/compat/qr39/mock_pas_callbacks.c \
		src/compat/qr39/mock_navicore.c
	python3 tests/check_cross_version_concrete_vtable_audit.py \
		evidence/qr39/cross-version/concrete-vtable-audit.csv \
		contracts/qr39/callbacks/proxy-slot-manifest.csv
	python3 tests/check_concrete_callback_integration.py \
		evidence/qr39/cross-version/concrete-vtable-audit.csv \
		contracts/qr39/callbacks/proxy-slot-manifest.csv \
		include/mock_pas_callbacks.h src/compat/qr39/mock_pas_callbacks.c \
		tests/pas_callbacks_smoke.c src/compat/qr39/mock_navicore.c

qnx-arm-abi-oracle-selftest:
	python3 tools/qnx_arm_abi_oracle.py --verify
	python3 tools/qnx_aggregate_abi_oracle.py --verify
	python3 tests/check_qnx_arm_abi_oracle.py \
		evidence/qr39/oracles/qnx-arm-catalog.csv \
		evidence/qr39/oracles/qnx-arm-assessment.csv \
		contracts/qr39/callbacks/proxy-slot-manifest.csv
	python3 tests/check_aggregate_callback_abi.py \
		evidence/qr39/oracles/aggregate-callback-abi.csv \
		evidence/qr39/cross-version/concrete-vtable-audit.csv \
		contracts/qr39/callbacks/proxy-slot-manifest.csv

arm-snapshot-replay-selftest:
	python3 tests/check_arm_snapshot_replay.py

raw-callback-capture-selftest: $(RAW_CAPTURE_TARGET)
	python3 tests/check_raw_callback_capture.py
	$(CC) -std=c99 -O2 -g -Wall -Wextra -Werror \
		-DMOCK_RAW_CALLBACK_TESTING -DMOCK_RAW_CALLBACK_HOST_TEST \
		$(CPPFLAGS) src/capture/mock_raw_callback_capture.c \
		src/capture/generated/mock_raw_callback_slot_table.c tests/raw_callback_capture_smoke.c \
		-o $(BUILD_DIR)/raw-callback-capture-smoke
	$(BUILD_DIR)/raw-callback-capture-smoke
	LD_LIBRARY_PATH=$(HOST_LIBS) python3 tests/check_raw_callback_arm.py \
		$(QNX_OBJDUMP) $(BUILD_DIR)/mock_raw_callback_trampolines_arm.o \
		$(BUILD_DIR)/mock_raw_callback_slots_arm.o
	LD_LIBRARY_PATH=$(HOST_LIBS) $(QNX_READELF) -h $(RAW_CAPTURE_TARGET) | \
		grep -q 'Machine:.*ARM'

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
