# Open NaviCore
#
#   make         build the product for the radio (build/qnx)
#   make ifs     navigation.ifs (build/qnx/ifs/navigation.ifs); needs
#                STOCK_NV_NAVIGATION_CFG=<factory nv_navigation.cfg> once
#   make test    host tests and contract checks (build/host)
#   make clean
#
# Toolchain locations: QNX_SDK, QNX_HOST_LIBS, OSMAND_QNX_ROOT and
# IFSBUILD_DIR, as variables or in the environment (defaults: the workspace
# that holds this repository, see cmake/qnx-armv7.cmake).
BUILD ?= build
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
CONFIG_VARS := QNX_SDK QNX_HOST_LIBS OSMAND_QNX_ROOT IFSBUILD_DIR STOCK_NV_NAVIGATION_CFG
CMAKE_DEFS := $(foreach v,$(CONFIG_VARS),$(if $($(v)),-D$(v)=$($(v))))

.PHONY: all ifs test clean
all:
	cmake -S . -B $(BUILD)/qnx -DCMAKE_TOOLCHAIN_FILE=cmake/qnx-armv7.cmake $(CMAKE_DEFS) >/dev/null
	cmake --build $(BUILD)/qnx -j $(JOBS)

ifs: all
	cmake --build $(BUILD)/qnx --target ifs

test:
	cmake -S . -B $(BUILD)/host $(CMAKE_DEFS) >/dev/null
	cmake --build $(BUILD)/host -j $(JOBS)
	ctest --test-dir $(BUILD)/host --output-on-failure -j $(JOBS)

clean:
	rm -rf $(BUILD)
