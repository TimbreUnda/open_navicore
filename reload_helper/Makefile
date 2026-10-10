QNX_SDK ?= /mnt/d/tools/qnx660-sdk
QNX_HOST := $(QNX_SDK)/host/linux/x86
QNX_TARGET := $(QNX_SDK)/target/qnx6
HOST_LIBS ?= /mnt/d/tools/qnx-host-libs
QCC := $(QNX_HOST)/usr/bin/QCC
READELF := $(QNX_HOST)/usr/bin/ntoarmv7-readelf

BUILD_DIR ?= /tmp/navicore-reload-build
DIST_DIR ?= ../dist
TARGET := $(DIST_DIR)/navicore_reload
OBJECT := $(BUILD_DIR)/navicore_reload.o

CXXFLAGS := -Vgcc_ntoarmv7le -std=c++98 -O2 -g -Wall -Wextra -Werror
LDFLAGS := -Vgcc_ntoarmv7le -pthread -Wl,-E
LDLIBS :=

.PHONY: all verify clean

all: $(TARGET)

$(TARGET): $(OBJECT) | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJECT): navicore_reload.cpp Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

verify: $(TARGET)
	LD_LIBRARY_PATH=$(HOST_LIBS) $(READELF) -h $(TARGET) | grep -q 'Machine:.*ARM'
	strings $(TARGET) | grep -q '^libnv_navigation_if.so$$'
	strings $(TARGET) | grep -q '^libns_framework_unified.so$$'
	LD_LIBRARY_PATH=$(HOST_LIBS) $(READELF) -Ws $(TARGET) | \
		grep -q 'GLOBAL.*g_PasLogParams'
	LD_LIBRARY_PATH=$(HOST_LIBS) $(READELF) -Ws $(TARGET) | \
		grep -q 'GLOBAL.*g_PasVersion'
	@echo "verified ARM/QNX helper loads the genuine PAS DSOs by SONAME"

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
