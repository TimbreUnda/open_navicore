QNX_SDK ?= /mnt/d/tools/qnx660-sdk
QNX_HOST := $(QNX_SDK)/host/linux/x86
QNX_TARGET := $(QNX_SDK)/target/qnx6
HOST_LIBS ?= /mnt/d/tools/qnx-host-libs
QCC := $(QNX_HOST)/usr/bin/QCC
READELF := $(QNX_HOST)/usr/bin/ntoarmv7-readelf

BUILD_DIR ?= /tmp/sm-reload-probe-build
DIST_DIR ?= ../dist
TARGET := $(DIST_DIR)/sm_reload_probe
OBJECT := $(BUILD_DIR)/sm_reload_probe.o

CXXFLAGS := -Vgcc_ntoarmv7le -std=c++98 -O2 -g -Wall -Wextra -Werror
LDFLAGS := -Vgcc_ntoarmv7le -pthread -Wl,-E

.PHONY: all verify test clean

all: $(TARGET)

$(TARGET): $(OBJECT) | $(DIST_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(LDFLAGS) -o $@ $^

$(OBJECT): sm_reload_probe.cpp sm_reload_abi.h Makefile | $(BUILD_DIR)
	QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) LD_LIBRARY_PATH=$(HOST_LIBS) \
		$(QCC) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

verify: $(TARGET)
	LD_LIBRARY_PATH=$(HOST_LIBS) $(READELF) -h $(TARGET) | grep -q 'Machine:.*ARM'
	strings $(TARGET) | grep -q '^libss_service_if_unified.so$$'
	strings $(TARGET) | grep -q '^libns_framework_unified.so$$'
	LD_LIBRARY_PATH=$(HOST_LIBS) $(READELF) -Ws $(TARGET) | grep -q 'GLOBAL.*g_PasLogParams'
	LD_LIBRARY_PATH=$(HOST_LIBS) $(READELF) -Ws $(TARGET) | grep -q 'GLOBAL.*g_PasVersion'
	strings $(TARGET) | grep -q 'start service=nv_navigation'
	strings $(TARGET) | grep -q 'AppStartResponse transport='
	@echo "verified ARM/QNX PAS probe/start client"

test: | $(BUILD_DIR)
	$(CXX) -std=c++98 -O2 -g -Wall -Wextra -Werror -I. \
		abi_test.cpp -o $(BUILD_DIR)/abi-test
	$(BUILD_DIR)/abi-test
	grep -q 'g_session_vtable\[9\].*on_start_response' sm_reload_probe.cpp
	grep -q 'on_session_creation_response' sm_reload_probe.cpp
	grep -q 'SendAppStartRequestER20_SS_ServiceStartInfoj' sm_reload_probe.cpp
	! grep -Eq 'SendApp(Stop|Launch|Terminate)Request' sm_reload_probe.cpp
	sh test_registered_identity.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
