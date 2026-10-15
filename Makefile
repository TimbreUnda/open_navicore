QNX_SDK ?=
QNX_HOST ?= $(if $(QNX_SDK),$(QNX_SDK)/host/linux/x86)
QNX_TARGET ?= $(if $(QNX_SDK),$(QNX_SDK)/target/qnx6)
HOST_LIBS ?= $(QNX_HOST_LIBS)
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
# Keep the host provider composition and renderer seam shared by smoke tests.
HOST_RENDERER_SOURCES := src/renderer/mock_renderer.c src/renderer/mock_screen_input.c \
	src/backend/mock_default_backend.c src/platform/host/mock_renderer_stub.c
HOST_PROVIDER_SOURCES := src/compat/qr39/navicore_compat.c src/runtime/mock_api_telemetry.c \
	src/runtime/mock_callback_backend.c src/runtime/mock_cluster_backend.c \
	src/backend/mock_backend_loader.c src/compat/qr39/host_callbacks.c src/runtime/mock_gps_backend.c \
	src/runtime/mock_pointer_backend.c src/runtime/mock_scalar_backend.c $(HOST_RENDERER_SOURCES)

PRIVATE_INCLUDE_DIRS := -Isrc/compat/qr39 -Isrc/runtime -Isrc/backend \
	-Isrc/renderer
CPPFLAGS := -I$(CURDIR)/include $(PRIVATE_INCLUDE_DIRS)
CFLAGS := -Vgcc_ntoarmv7le -std=c99 -O2 -g -Wall -Wextra -Werror \
	-D_REENTRANT -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections
LDFLAGS := -Vgcc_ntoarmv7le -shared -Wl,-soname,libNaviCore.so \
	-Wl,--gc-sections -Wl,--no-undefined

.PHONY: all verify-qnx test test-host check-contracts provider-coverage-selftest \
	static-provider-coverage-selftest navigation-ui-test portability-selftest \
	osmand-backend provider backend osmand-obf-smoke osmand-plugin-smoke clean

all: $(TARGET) $(SMOKE_TARGET) $(PLUGIN_TARGET) $(OUTBOUND_PLUGIN_TARGET) \
	$(PLUGIN_SMOKE_TARGET)

# Product entry points: the provider (qcc) and the OsmAnd backend (osmand.mk).
provider: $(TARGET)
backend: osmand-backend

include backends/osmand/osmand.mk

$(TARGET): $(OBJECTS) | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(LDFLAGS) -o $@ $^ -lscreen -lEGL -lGLESv2

$(BUILD_DIR)/mock_navicore.o: src/compat/qr39/navicore_compat.c src/compat/qr39/provider_slots.def src/backend/mock_backend_loader.h include/mock_navicore.h include/mock_api_telemetry.h include/mock_callback_backend.h include/open_navicore/cluster.h include/mock_gps_backend.h include/mock_pointer_backend.h include/mock_scalar_backend.h include/mock_renderer.h include/open_navicore/backend.h Makefile | $(BUILD_DIR)
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

$(BUILD_DIR)/mock_pas_callbacks.o: src/compat/qr39/host_callbacks.c include/open_navicore/host_callbacks.h include/mock_callback_backend.h Makefile | $(BUILD_DIR)
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
# only committed source and contracts; no QNX SDK or stock ELF.
check-contracts: portability-selftest
	python3 -m tests.check_symbol_names
	sh tests/check_plugin_elf.sh --self-test
	sh tests/check_callback_coverage.sh $(CALLBACK_MATRIX) contracts/qr39/callbacks/runtime-coverage.csv src/compat/qr39/navicore_compat.c
	python3 -m tests.check_static_callback_additions contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv
	python3 -m tests.check_static_callback_contracts \
		contracts/qr39/callbacks/static-contracts.csv contracts/qr39/callbacks/runtime-coverage.csv \
		contracts/qr39/callbacks/dispatch-additions.csv include/open_navicore/host_callbacks.h \
		src/compat/qr39/host_callbacks.c src/compat/qr39/navicore_compat.c
	python3 -m tests.check_provider_coverage $(PROVIDER_MATRIX) $(POINTER_MATRIX) \
		contracts/qr39/provider/api-coverage.csv src/compat/qr39/navicore_compat.c
	python3 -m tests.check_static_provider_slots contracts/qr39/provider/slot-manifest.csv \
		src/compat/qr39/navicore_compat.c src/compat/qr39/provider_slots.def

verify-qnx: $(TARGET) $(SMOKE_TARGET) $(PLUGIN_TARGET) $(OUTBOUND_PLUGIN_TARGET) \
	$(PLUGIN_SMOKE_TARGET) check-contracts
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

test-host: check-contracts osmand-session-test navigation-ui-test \
	camera-controller-test labels-test vector-test
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
		src/runtime/mock_callback_backend.c src/compat/qr39/host_callbacks.c \
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
	python3 -m tests.check_provider_coverage $(PROVIDER_MATRIX) $(POINTER_MATRIX) \
		contracts/qr39/provider/api-coverage.csv src/compat/qr39/navicore_compat.c --self-test

static-provider-coverage-selftest:
	python3 -m tests.check_static_provider_slots contracts/qr39/provider/slot-manifest.csv \
		src/compat/qr39/navicore_compat.c src/compat/qr39/provider_slots.def --self-test

portability-selftest:
	python3 -m tests.check_portable_paths

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
