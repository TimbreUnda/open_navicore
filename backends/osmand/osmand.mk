# Optional modern-C++ toolchain; the provider/reference backend keep qcc.
OSMAND_QNX_ROOT ?=
OSMAND_CORE_LIB_DIR ?= $(OSMAND_QNX_ROOT)/source/binaries/qnx/gcc-armv7/Release
OSMAND_LEGACY_ROOT ?= $(OSMAND_QNX_ROOT)/source/core-legacy
OSMAND_LEGACY_SRC_DIR := $(OSMAND_LEGACY_ROOT)/native/src
OSMAND_QT_PREFIX ?= $(OSMAND_QNX_ROOT)/qt-pic-prefix
OSMAND_CXX ?= $(OSMAND_QNX_ROOT)/gcc-out/bin/arm-unknown-nto-qnx6.6.0eabi-g++
OSMAND_CC ?= $(OSMAND_QNX_ROOT)/gcc-out/bin/arm-unknown-nto-qnx6.6.0eabi-gcc
# Change this identifier when replacing an external binary/source bundle in place.
OSMAND_BUNDLE_ID ?= local-unqualified
OSMAND_CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Werror \
	-Wno-error=ignored-attributes -Wno-error=deprecated-declarations \
	-Wno-error=unused-function -Wno-error=unused-parameter \
	-Wno-error=unused-variable -Wno-error=sign-compare \
	-Wno-error=ignored-qualifiers \
	-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp \
	-D_HAS_C9X=1 -D_NO_CPP_INLINES=1 -D_HAS_GENERIC_TEMPLATES=0 \
	-DOSMAND_COMPILER_FAMILY_gcc -DOSMAND_TARGET_OS_qnx \
	-DOSMAND_TARGET_CPU_ARCH_armv7 -DOSMAND_TARGET_CPU_ARCH_FAMILY_arm \
	-DOSMAND_OPENGLES2PLUS_RENDERER_SUPPORTED -DSK_SUPPORT_LEGACY_DRAWLOOPER \
	-fPIC -fvisibility=hidden -ffunction-sections -fdata-sections
OSMAND_INCLUDES := -I$(CURDIR)/include -I$(OSMAND_QNX_ROOT)/source/core/include \
	-I$(CURDIR) -I$(CURDIR)/third_party/lvgl \
	-I$(OSMAND_LEGACY_ROOT)/native/include -I$(OSMAND_LEGACY_SRC_DIR) \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/protobuf/upstream.patched/src \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/glm/upstream.patched \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/boost/upstream.patched \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/skia/upstream.patched \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/skia/upstream.patched/include/core \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/skia/upstream.patched/include/effects \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/skia/upstream.patched/src/core \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/expat/upstream.patched/lib \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/harfbuzz/upstream.patched/src \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/freetype/upstream.patched/include \
	-I$(OSMAND_QT_PREFIX)/include -I$(OSMAND_QT_PREFIX)/include/QtCore \
	-I$(OSMAND_QT_PREFIX)/include/QtNetwork
OSMAND_LINKFLAGS := -shared -static-libstdc++ -static-libgcc -Wl,-Bsymbolic \
	-Wl,-soname,libMockNavBackendOsmAnd.so \
	-Wl,-rpath,/datastore/navicore-backend \
	-Wl,--version-script=$(CURDIR)/backends/osmand/osmand_backend.map \
	-Wl,--gc-sections -Wl,--no-undefined -Wl,-z,text
OSMAND_LVGL_FLAGS := -DLV_CONF_PATH='"backends/osmand/ui/lv_conf.h"'
# Worktrees, flags, toolchains and bundle identities never share object files.
OSMAND_CONFIG_KEY := $(shell printf '%s\n' '$(CURDIR)' '$(QNX_HOST)' \
	'$(QNX_TARGET)' '$(HOST_LIBS)' '$(OSMAND_CXX)' '$(OSMAND_CORE_LIB_DIR)' \
	'$(OSMAND_CC)' \
	'$(OSMAND_BUNDLE_ID)' '$(OSMAND_CXXFLAGS)' '$(OSMAND_INCLUDES)' \
	'$(OSMAND_LVGL_FLAGS)' '$(OSMAND_LINKFLAGS)' '$(CPPFLAGS)' '$(CFLAGS)' | sha256sum | cut -c1-16)
OSMAND_BUILD_ROOT ?= $(CURDIR)/build/osmand
OSMAND_BUILD_DIR := $(OSMAND_BUILD_ROOT)/$(OSMAND_CONFIG_KEY)
OSMAND_BINUTILS_DIR := $(OSMAND_BUILD_DIR)/binutils
OSMAND_BINUTILS_STAMP := $(OSMAND_BINUTILS_DIR)/.ready
OSMAND_PLUGIN_TARGET := $(OSMAND_BUILD_DIR)/libMockNavBackendOsmAnd.so
OSMAND_OBJECTS := $(addprefix $(OSMAND_BUILD_DIR)/,osmand_backend.o config.o \
	session.o diagnostics.o osmand_engine.o navigation_worker.o compact_text.o navigation_model.o \
	guidance_tracker.o navigation_view.o gl_overlay_compositor.o frame_metrics.o)
LVGL_SOURCES := $(shell find third_party/lvgl/src -type f -name '*.c')
OSMAND_LVGL_OBJECTS := $(patsubst third_party/lvgl/src/%.c,$(OSMAND_BUILD_DIR)/lvgl/%.o,$(LVGL_SOURCES))
OSMAND_OBJECTS += $(OSMAND_LVGL_OBJECTS)
OSMAND_OBF_SMOKE_TARGET := $(OSMAND_BUILD_DIR)/qnx-osmand-obf-smoke
OSMAND_ROUTE_SMOKE_TARGET := $(OSMAND_BUILD_DIR)/qnx-osmand-route-smoke
OSMAND_PLUGIN_SMOKE_TARGET := $(OSMAND_BUILD_DIR)/qnx-osmand-plugin-smoke
OSMAND_ENV = QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS)
OSMAND_COMPILE = $(OSMAND_ENV) $(OSMAND_CXX) --sysroot=$(QNX_TARGET) \
	-B$(OSMAND_BINUTILS_DIR)/ $(OSMAND_CXXFLAGS) $(OSMAND_INCLUDES) $(OSMAND_LVGL_FLAGS)

.PHONY: osmand-check osmand-manifest osmand-session-test osmand-route-smoke

osmand-backend: $(OSMAND_PLUGIN_TARGET) | $(DIST_DIR)
	cp $< $(DIST_DIR)/libMockNavBackendOsmAnd.so

osmand-obf-smoke: $(OSMAND_OBF_SMOKE_TARGET) | $(DIST_DIR)
	cp $< $(DIST_DIR)/qnx-osmand-obf-smoke

osmand-route-smoke: $(OSMAND_ROUTE_SMOKE_TARGET) | $(DIST_DIR)
	cp $< $(DIST_DIR)/qnx-osmand-route-smoke

osmand-plugin-smoke: $(OSMAND_PLUGIN_SMOKE_TARGET) | $(DIST_DIR)
	cp $< $(DIST_DIR)/qnx-osmand-plugin-smoke

$(OSMAND_BUILD_DIR):
	mkdir -p $@

$(OSMAND_BINUTILS_STAMP): | $(OSMAND_BUILD_DIR)
	test -n "$(OSMAND_QNX_ROOT)"
	mkdir -p $(OSMAND_BINUTILS_DIR)
	ln -sf $(QNX_HOST)/usr/bin/arm-unknown-nto-qnx6.6.0eabi-as $(OSMAND_BINUTILS_DIR)/as
	ln -sf $(QNX_HOST)/usr/bin/arm-unknown-nto-qnx6.6.0eabi-ld $(OSMAND_BINUTILS_DIR)/ld
	touch $@

$(OSMAND_BUILD_DIR)/%.o: backends/osmand/%.cpp backends/osmand/osmand.mk \
		$(OSMAND_CXX) $(OSMAND_BINUTILS_STAMP)
	$(OSMAND_COMPILE) -MMD -MP -c $< -o $@

$(OSMAND_BUILD_DIR)/%.o: backends/osmand/ui/%.cpp backends/osmand/osmand.mk \
		$(OSMAND_CXX) $(OSMAND_BINUTILS_STAMP)
	$(OSMAND_COMPILE) -I$(CURDIR) -I$(CURDIR)/third_party/lvgl \
		$(OSMAND_LVGL_FLAGS) -MMD -MP -c $< -o $@

$(OSMAND_BUILD_DIR)/lvgl/%.o: third_party/lvgl/src/%.c \
		backends/osmand/ui/lv_conf.h backends/osmand/osmand.mk \
		$(OSMAND_CC) $(OSMAND_BINUTILS_STAMP)
	mkdir -p $(dir $@)
	$(OSMAND_ENV) $(OSMAND_CC) --sysroot=$(QNX_TARGET) -B$(OSMAND_BINUTILS_DIR)/ \
		-std=c99 -O2 -fPIC -ffunction-sections -fdata-sections \
		-I$(CURDIR) -I$(CURDIR)/third_party/lvgl \
		$(OSMAND_LVGL_FLAGS) -MMD -MP -c $< -o $@

$(OSMAND_PLUGIN_TARGET): $(OSMAND_OBJECTS) backends/osmand/osmand_backend.map \
		$(OSMAND_CORE_LIB_DIR)/libOsmAndCore_shared.so
	$(OSMAND_ENV) $(OSMAND_CXX) --sysroot=$(QNX_TARGET) -B$(OSMAND_BINUTILS_DIR)/ \
		$(OSMAND_LINKFLAGS) -o $@ $(OSMAND_OBJECTS) \
		-L$(OSMAND_CORE_LIB_DIR) -lOsmAndCore_shared -lEGL -lGLESv2 -lc

$(OSMAND_BUILD_DIR)/qnx_osmand_obf_smoke.o: tests/qnx_osmand_obf_smoke.cpp \
		backends/osmand/osmand.mk $(OSMAND_CXX) $(OSMAND_BINUTILS_STAMP)
	$(OSMAND_COMPILE) -MMD -MP -c $< -o $@

$(OSMAND_OBF_SMOKE_TARGET): $(OSMAND_BUILD_DIR)/qnx_osmand_obf_smoke.o \
		$(OSMAND_CORE_LIB_DIR)/libOsmAndCore_shared.so
	$(OSMAND_ENV) $(QCC) -Vgcc_ntoarmv7le -Wl,--no-undefined -o $@ $< \
		-L$(OSMAND_CORE_LIB_DIR) -lOsmAndCore_shared \
		-lsocket -lEGL -lGLESv2 -lslog2 -lz -lpps -lm

$(OSMAND_BUILD_DIR)/qnx_osmand_route_smoke.o: tests/qnx_osmand_route_smoke.cpp \
		backends/osmand/osmand.mk $(OSMAND_CXX) $(OSMAND_BINUTILS_STAMP)
	$(OSMAND_COMPILE) -MMD -MP -c $< -o $@

$(OSMAND_ROUTE_SMOKE_TARGET): $(OSMAND_BUILD_DIR)/qnx_osmand_route_smoke.o \
		$(OSMAND_CORE_LIB_DIR)/libOsmAndCore_shared.so
	$(OSMAND_ENV) $(OSMAND_CXX) --sysroot=$(QNX_TARGET) -B$(OSMAND_BINUTILS_DIR)/ \
		-static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined \
		-Wl,-rpath,/datastore/navicore-backend -o $@ $< \
		-L$(OSMAND_CORE_LIB_DIR) -lOsmAndCore_shared \
		-lsocket -lslog2 -lz -lpps -lEGL -lGLESv2 -lm -lc

$(OSMAND_BUILD_DIR)/qnx_osmand_plugin_smoke.o: tests/qnx_osmand_plugin_smoke.c \
		backends/osmand/osmand.mk | $(OSMAND_BUILD_DIR)
	$(OSMAND_ENV) $(QCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OSMAND_PLUGIN_SMOKE_TARGET): $(OSMAND_BUILD_DIR)/qnx_osmand_plugin_smoke.o
	$(OSMAND_ENV) $(QCC) -Vgcc_ntoarmv7le -o $@ $<

-include $(OSMAND_OBJECTS:.o=.d) $(OSMAND_BUILD_DIR)/qnx_osmand_obf_smoke.d \
	$(OSMAND_BUILD_DIR)/qnx_osmand_route_smoke.d \
	$(OSMAND_BUILD_DIR)/qnx_osmand_plugin_smoke.d

osmand-check: osmand-backend
	LD_LIBRARY_PATH=$(HOST_LIBS) READELF=$(QNX_READELF) \
		sh tests/check_osmand_elf.sh $(OSMAND_PLUGIN_TARGET)

osmand-manifest: osmand-backend
	$(OSMAND_ENV) python3 tools/osmand_manifest.py --root "$(OSMAND_QNX_ROOT)" \
		--core-lib-dir "$(OSMAND_CORE_LIB_DIR)" --qt-prefix "$(OSMAND_QT_PREFIX)" \
		--compiler "$(OSMAND_CXX)" --bundle-id "$(OSMAND_BUNDLE_ID)" \
		--qnx-host "$(QNX_HOST)" --qnx-target "$(QNX_TARGET)" \
		--config-key "$(OSMAND_CONFIG_KEY)" \
		--plugin "$(OSMAND_PLUGIN_TARGET)" \
		--output "$(OSMAND_BUILD_DIR)/bundle-manifest.json"

osmand-session-test:
	mkdir -p $(BUILD_DIR)
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -Werror -pthread \
		-Iinclude -Ibackends/osmand -Itests/fixtures/fake_egl \
		backends/osmand/config.cpp backends/osmand/session.cpp \
		backends/osmand/osmand_backend.cpp tests/osmand_session_test.cpp \
		-o $(BUILD_DIR)/osmand-session-test
	$(BUILD_DIR)/osmand-session-test

LVGL_HOST_BUILD_DIR := $(BUILD_DIR)/lvgl-host
LVGL_HOST_OBJECTS := $(patsubst third_party/lvgl/src/%.c,$(LVGL_HOST_BUILD_DIR)/%.o,$(LVGL_SOURCES))

$(LVGL_HOST_BUILD_DIR)/%.o: third_party/lvgl/src/%.c backends/osmand/ui/lv_conf.h
	mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -I$(CURDIR) -I$(CURDIR)/third_party/lvgl \
		-DLV_CONF_PATH='"backends/osmand/ui/lv_conf.h"' -c $< -o $@

$(LVGL_HOST_BUILD_DIR)/liblvgl.a: $(LVGL_HOST_OBJECTS)
	$(AR) rcs $@ $^

navigation-ui-test: $(LVGL_HOST_BUILD_DIR)/liblvgl.a
	$(CXX) -std=c++17 -O2 -g -Wall -Wextra -Werror \
		-Iinclude -Ibackends/osmand -Ithird_party/lvgl -I$(CURDIR) \
		-DLV_CONF_PATH='"backends/osmand/ui/lv_conf.h"' \
		backends/osmand/diagnostics.cpp \
		backends/osmand/guidance_tracker.cpp \
		backends/osmand/ui/compact_text.cpp \
		backends/osmand/ui/navigation_model.cpp \
		backends/osmand/ui/navigation_view.cpp tests/navigation_ui_test.cpp \
		$(LVGL_HOST_BUILD_DIR)/liblvgl.a -lm -o $(LVGL_HOST_BUILD_DIR)/navigation-ui-test
	$(LVGL_HOST_BUILD_DIR)/navigation-ui-test
