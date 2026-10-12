# Bench tools for QNX Screen presentation cost. Build on the host:
#   make -C tools/qnx/screen_bench QNX_SDK=/path/to/qnx660-sdk QNX_HOST_LIBS=/path
# Copy screen_bench, screen_survey, survey_debugfs.sh and run_sweep.sh to
# /tmp on the radio (RAM-backed, gone after reboot).
QNX_SDK ?=
QNX_HOST ?= $(QNX_SDK)/host/linux/x86
QNX_TARGET ?= $(QNX_SDK)/target/qnx6
QNX_HOST_LIBS ?=
QCC := QNX_HOST=$(QNX_HOST) QNX_TARGET=$(QNX_TARGET) \
	LD_LIBRARY_PATH=$(QNX_HOST_LIBS) $(QNX_HOST)/usr/bin/qcc
CFLAGS := -Vgcc_ntoarmv7le -std=c99 -O2 -Wall -Wextra -Werror
OUT ?= $(CURDIR)/../../../dist/screen_bench_tools

all: $(OUT)/screen_bench $(OUT)/screen_survey $(OUT)/gpu_bench \
	$(OUT)/cpu_bench $(OUT)/cpu_bench_neon

$(OUT):
	mkdir -p $@

$(OUT)/screen_bench: screen_bench.c | $(OUT)
	$(QCC) $(CFLAGS) $< -lscreen -lEGL -lGLESv2 -o $@

$(OUT)/screen_survey: screen_survey.c | $(OUT)
	$(QCC) $(CFLAGS) $< -lscreen -o $@

$(OUT)/gpu_bench: gpu_bench.c | $(OUT)
	$(QCC) $(CFLAGS) $< -lscreen -lEGL -lGLESv2 -o $@

# Same code twice: the backend's current FPU flags vs NEON.
$(OUT)/cpu_bench: cpu_bench.c | $(OUT)
	$(QCC) $(CFLAGS) -O3 -Wc,-mfpu=vfpv3-d16 -Wc,-mfloat-abi=softfp $< -lz -lm -o $@

$(OUT)/cpu_bench_neon: cpu_bench.c | $(OUT)
	$(QCC) $(CFLAGS) -O3 -Wc,-mfpu=neon -Wc,-mfloat-abi=softfp \
		-Wc,-ftree-vectorize $< -lz -lm -o $@

clean:
	rm -f $(OUT)/screen_bench $(OUT)/screen_survey $(OUT)/gpu_bench \
		$(OUT)/cpu_bench $(OUT)/cpu_bench_neon

.PHONY: all clean
