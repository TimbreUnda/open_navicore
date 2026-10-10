# Optional modern-C++ toolchain; the provider/reference backend keep qcc.
OSMAND_QNX_ROOT ?=
OSMAND_CORE_LIB_DIR ?= $(OSMAND_QNX_ROOT)/source/binaries/qnx/gcc-armv7/Release
OSMAND_QT_PREFIX ?= $(OSMAND_QNX_ROOT)/qt-pic-prefix
OSMAND_CXX ?= $(OSMAND_QNX_ROOT)/gcc-out/bin/arm-unknown-nto-qnx6.6.0eabi-g++
# Change this identifier when replacing an external binary/source bundle in place.
OSMAND_BUNDLE_ID ?= local-unqualified
OSMAND_CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Werror \
	-Wno-error=ignored-attributes -Wno-error=deprecated-declarations \
	-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp \
	-D_HAS_C9X=1 -D_NO_CPP_INLINES=1 -D_HAS_GENERIC_TEMPLATES=0 \
	-DOSMAND_COMPILER_FAMILY_gcc -DOSMAND_TARGET_OS_qnx \
	-DOSMAND_TARGET_CPU_ARCH_armv7 -DOSMAND_TARGET_CPU_ARCH_FAMILY_arm \
	-DOSMAND_OPENGLES2PLUS_RENDERER_SUPPORTED \
	-fPIC -fvisibility=hidden -ffunction-sections -fdata-sections
OSMAND_INCLUDES := -I$(CURDIR)/include -I$(OSMAND_QNX_ROOT)/source/core/include \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/glm/upstream.patched \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/boost/upstream.patched \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/skia/upstream.patched \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/skia/upstream.patched/include/core \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/harfbuzz/upstream.patched/src \
	-I$(OSMAND_QNX_ROOT)/source/core/externals/freetype/upstream.patched/include \
	-I$(OSMAND_QT_PREFIX)/include -I$(OSMAND_QT_PREFIX)/include/QtCore \
	-I$(OSMAND_QT_PREFIX)/include/QtNetwork
OSMAND_LINKFLAGS := -shared -static-libstdc++ -static-libgcc -Wl,-Bsymbolic \
	-Wl,-soname,libMockNavBackendOsmAnd.so \
	-Wl,-rpath,/datastore/navicore-backend \
	-Wl,--version-script=$(CURDIR)/backends/osmand/osmand_backend.map \
	-Wl,--gc-sections -Wl,--no-undefined -Wl,-z,text
# Worktrees, flags, toolchains and bundle identities never share object files.
OSMAND_CONFIG_KEY := $(shell printf '%s\n' '$(CURDIR)' '$(QNX_HOST)' \
	'$(QNX_TARGET)' '$(HOST_LIBS)' '$(OSMAND_CXX)' '$(OSMAND_CORE_LIB_DIR)' \
	'$(OSMAND_BUNDLE_ID)' '$(OSMAND_CXXFLAGS)' '$(OSMAND_INCLUDES)' \
	'$(OSMAND_LINKFLAGS)' '$(CPPFLAGS)' '$(CFLAGS)' | sha256sum | cut -c1-16)
OSMAND_BUILD_ROOT ?= $(CURDIR)/build/osmand
OSMAND_BUILD_DIR := $(OSMAND_BUILD_ROOT)/$(OSMAND_CONFIG_KEY)
OSMAND_BINUTILS_DIR := $(OSMAND_BUILD_DIR)/binutils
OSMAND_BINUTILS_STAMP := $(OSMAND_BINUTILS_DIR)/.ready
OSMAND_PLUGIN_TARGET := $(OSMAND_BUILD_DIR)/libMockNavBackendOsmAnd.so
OSMAND_OBJECTS := $(addprefix $(OSMAND_BUILD_DIR)/,osmand_backend.o config.o \
	session.o diagnostics.o osmand_engine.o)
OSMAND_OBF_SMOKE_TARGET := $(OSMAND_BUILD_DIR)/qnx-osmand-obf-smoke
OSMAND_PLUGIN_SMOKE_TARGET := $(OSMAND_BUILD_DIR)/qnx-osmand-plugin-smoke
OSMAND_ENV = QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS)
OSMAND_COMPILE = $(OSMAND_ENV) $(OSMAND_CXX) --sysroot=$(QNX_TARGET) \
	-B$(OSMAND_BINUTILS_DIR)/ $(OSMAND_CXXFLAGS) $(OSMAND_INCLUDES)

.PHONY: osmand-check osmand-manifest osmand-session-test

osmand-backend: $(OSMAND_PLUGIN_TARGET) | $(DIST_DIR)
	cp $< $(DIST_DIR)/libMockNavBackendOsmAnd.so

osmand-obf-smoke: $(OSMAND_OBF_SMOKE_TARGET) | $(DIST_DIR)
	cp $< $(DIST_DIR)/qnx-osmand-obf-smoke

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

$(OSMAND_BUILD_DIR)/qnx_osmand_plugin_smoke.o: tests/qnx_osmand_plugin_smoke.c \
		backends/osmand/osmand.mk | $(OSMAND_BUILD_DIR)
	$(OSMAND_ENV) $(QCC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OSMAND_PLUGIN_SMOKE_TARGET): $(OSMAND_BUILD_DIR)/qnx_osmand_plugin_smoke.o
	$(OSMAND_ENV) $(QCC) -Vgcc_ntoarmv7le -o $@ $<

-include $(OSMAND_OBJECTS:.o=.d) $(OSMAND_BUILD_DIR)/qnx_osmand_obf_smoke.d \
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
