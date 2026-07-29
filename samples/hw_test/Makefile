# openagc hardware validation samples — Makefile
#
# Three test programs adapted from freegnm-examples:
#   videoout_linear.elf — VideoOut display pipeline smoke test (no GPU)
#   agc_init.elf        — AGC init + NOP submit + queue test (GPU commands)
#   agc_videoout.elf    — Combined AGC + VideoOut test (GPU + display)
#
# Prerequisites:
#   export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
#   Build openagc first: cmake --build build-prospero
#   Build LibProsperoPkg for eboot.bin: see AGENTS.md

PS5_HOST ?= 10.0.1.41
PS5_PORT ?= 9021

TITLE_ID := AGCT00001

# Save this Makefile's directory before including prospero.mk
THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

# Path to openagc root
OPENAGC_ROOT := $(abspath $(THIS_DIR)/../..)
OPENAGC_LIB  := $(OPENAGC_ROOT)/build-prospero/libopenagc.a
OPENAGC_INC  := $(OPENAGC_ROOT)/include

# LibProsperoPkg tools (for eboot.bin and .pkg packaging)
PKG_TOOLS ?= /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build
FSELF     := $(PKG_TOOLS)/prosperopkg-fself
PKGBUILD  := $(PKG_TOOLS)/prosperopkg-pkg
PSBC      ?= $(OPENAGC_ROOT)/../openagc-psbc/psbc
GLSLANG   ?= glslangValidator
READELF   ?= /opt/homebrew/opt/llvm@18/bin/llvm-readelf

PS4_SDK_VERSION ?= 0x07590001

# PS5 packaging constants
CONTENT_ID  := UP9000-AGCT00001_00-OPENAGC000000000
PASSCODE    := 00000000000000000000000000000000

CFLAGS  := -Wall -Wextra -g -I$(OPENAGC_INC) -I$(THIS_DIR) $(EXTRA_CFLAGS)

# videoout_linear: no AGC dependency, just VideoOut + kernel
VIDEOOUT_LDFLAGS := -lSceVideoOut -lkernel

# agc_init: links libopenagc.a + VideoOut + kernel
AGC_LDFLAGS := $(OPENAGC_LIB) -lSceVideoOut -lkernel
FW_STAGE_LDFLAGS := $(OPENAGC_LIB) -lSceVideoOut -lkernel
FW_SONY_ORACLE_LDFLAGS := $(OPENAGC_LIB) -lkernel
PROCESS_CLEANUP_ELF ?= $(OPENAGC_ROOT)/../Vulkan-PS5/build-prospero-m2/vulkan_ps5_process_cleanup.elf

# === Targets ===

all: videoout_linear.elf agc_init.elf agc_videoout.elf agc_compute.elf agc_graphics.elf agc_depth.elf agc_depth_stencil.elf agc_depth_msaa.elf agc_tessellation.elf agc_tess_geometry.elf agc_tess_geometry_invocations.elf agc_tess_geometry_lines.elf agc_tess_geometry_rgba8.elf

FW550_CONFORMANCE_ELFS := \
	videoout_linear.elf agc_init.elf agc_videoout.elf agc_compute.elf \
	agc_graphics.elf agc_graphics_rgba8.elf agc_graphics_amplify.elf \
	agc_graphics_lines.elf agc_graphics_invocations.elf \
	agc_tessellation.elf agc_tess_geometry.elf \
	agc_tess_geometry_invocations.elf agc_tess_geometry_lines.elf \
	agc_tess_geometry_rgba8.elf \
	agc_depth_htile_ops.elf agc_depth_expclear.elf \
	agc_depth_stencil_htile.elf

.PHONY: conformance_fw550 conformance_fw550_check
conformance_fw550: $(FW550_CONFORMANCE_ELFS)
	PS5_HOST=$(PS5_HOST) ./run_fw550_conformance.sh

conformance_fw550_check: $(FW550_CONFORMANCE_ELFS)
	./run_fw550_conformance.sh --check

# VideoOut linear smoke test (no GPU commands)
videoout_linear.elf: videoout_linear.c ps5_video_out.h
	$(CC) $(CFLAGS) -o $@ videoout_linear.c $(VIDEOOUT_LDFLAGS)

# AGC init + NOP submit test (links libopenagc.a)
agc_init.elf: agc_init.c ps5_video_out.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_init.c $(AGC_LDFLAGS)

# FW 11.60 full public-path qualification artifact. It self-terminates after
# flushing its verdict so websrv cannot retain a completed foreground process.
agc_init_fw1160.elf: agc_init.c ps5_video_out.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_EXPECT_FIRMWARE_ABI_KEY=0x1160u \
		-DAGC_EXPECT_DEFAULT_STATES=1 -DAGC_EXPECT_WORKLOAD=0 \
		-DAGC_SELF_TERMINATE=1 \
		-o $@ agc_init.c $(AGC_LDFLAGS)

agc_fw1160_stage0.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=0 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage1.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=1 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage2.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=2 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage3.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=3 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage4.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=4 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage5.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=5 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage6.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=6 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage7.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=7 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage8.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=8 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage9.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=9 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage10.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=10 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage11.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=11 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage12.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=12 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage13.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=13 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage14.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=14 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage15.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=15 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage16.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=16 -o $@ $< $(FW_STAGE_LDFLAGS)

agc_fw1160_stage17.elf: agc_fw1160_stage.c gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_FW1160_STAGE=17 -o $@ $< $(FW_STAGE_LDFLAGS)

# Opt-in oracle for the console's own FW 11.60 libSceAgcDriver. This target
# intentionally has no SceAgcDriver DT_NEEDED entry: credentials must be
# patched before the payload calls dlopen(), and no direct backend is started.
agc_fw1160_sony_workload.elf: agc_fw1160_sony_workload.c \
		gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ $< $(FW_SONY_ORACLE_LDFLAGS)

.PHONY: fw1160_sony_workload_check
fw1160_sony_workload_check: agc_fw1160_sony_workload.elf
	@test -x "$(READELF)" || { echo "missing LLVM readelf: $(READELF)" >&2; exit 1; }
	@if $(READELF) -d $< | grep -q 'libSceAgcDriver\.sprx'; then \
		echo "$< must dlopen after credentials, not preload libSceAgcDriver" >&2; \
		exit 1; \
	fi
	@$(READELF) -p .rodata $< | grep -q 'libSceAgcDriver\.sprx' || { \
		echo "$< is missing the exact runtime module name" >&2; \
		exit 1; \
	}
	@$(READELF) -p .rodata $< | grep -q 'sceAgcDriverSubmitMultiDcbs' || { \
		echo "$< is missing the installed multi-DCB trailer path" >&2; \
		exit 1; \
	}
	@echo "FW 11.60 Sony workload oracle dependency split: PASS"

# Combined AGC + VideoOut test (links libopenagc.a + VideoOut + kernel)
agc_videoout.elf: agc_videoout.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_videoout.c $(AGC_LDFLAGS)

# Compute dispatch test (AGC + compute shader + VideoOut)
agc_compute.elf: agc_compute.c ps5_video_out.h gpu_credentials.h shaders/fill_color_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_compute.c $(AGC_LDFLAGS)

agc_compute_fw1160.elf: agc_compute.c ps5_video_out.h gpu_credentials.h shaders/fill_color_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_EXPECT_FIRMWARE_ABI_KEY=0x1160u \
		-DAGC_COMPUTE_HEADLESS=1 -DAGC_SELF_TERMINATE=1 \
		-o $@ agc_compute.c $(AGC_LDFLAGS)

shaders/htile_rmw.spv: shaders/htile_rmw.comp
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/htile_rmw.sb: shaders/htile_rmw.spv
	$(PSBC) -f $< -s compute -o $@

shaders/htile_rmw_sb.h: shaders/htile_rmw.sb
	xxd -i -n htile_rmw_sb $< $@

.PHONY: shaders_htile_rmw
shaders_htile_rmw: shaders/htile_rmw_sb.h

# Regenerate SPIR-V whenever the editable vertex shader changes.
shaders/triangle_vert.spv: shaders/triangle.vert
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_frag.spv: shaders/triangle.frag
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_line_frag.spv: shaders/triangle_line.frag
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_geom.spv: shaders/triangle.geom
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_tesc.spv: shaders/triangle.tesc
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_tess_vert.spv: shaders/triangle_tess.vert
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_tese.spv: shaders/triangle.tese
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_tess_geom.spv: shaders/triangle_tess.geom
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_frag.sb: shaders/triangle_frag.spv
	$(PSBC) -f $< -s fragment -o $@

shaders/triangle_frag_sb.h: shaders/triangle_frag.sb
	xxd -i -n triangle_frag_data $< $@

shaders/triangle_line_frag.sb: shaders/triangle_line_frag.spv
	$(PSBC) -f $< -s fragment -o $@

shaders/triangle_line_frag_sb.h: shaders/triangle_line_frag.sb
	xxd -i -n triangle_line_frag_data $< $@

shaders/depth_triangle_vert.spv: shaders/depth_triangle.vert
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/depth_triangle_frag.spv: shaders/depth_triangle.frag
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/depth_triangle_frag.sb: shaders/depth_triangle_frag.spv
	$(PSBC) -f $< -s fragment -o $@

shaders/depth_triangle_frag_sb.h: shaders/depth_triangle_frag.sb
	xxd -i -n depth_triangle_frag_data $< $@

shaders/depth_resolve_frag.spv: shaders/depth_resolve.frag
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/depth_resolve_frag.sb: shaders/depth_resolve_frag.spv
	$(PSBC) -f $< -s fragment -o $@

shaders/depth_resolve_frag_sb.h: shaders/depth_resolve_frag.sb
	xxd -i -n depth_resolve_frag_data $< $@

.PHONY: shaders_depth
shaders_depth: shaders/depth_triangle_vert.spv shaders/triangle_geom.spv \
               shaders/depth_triangle_frag_sb.h
	$(PSBC) -f shaders/triangle_geom.spv -s geometry \
	    --pre-vertex shaders/depth_triangle_vert.spv \
	    --wave32 \
	    -o shaders/depth_triangle_ngg_back.sb \
	    --ngg-front shaders/depth_triangle_ngg_front.sb \
	    --vertex-attribute 0:0:0:24:r32g32b32_float \
	    --vertex-attribute 1:0:12:24:r32g32b32_float
	xxd -i -n depth_triangle_ngg_front_data \
	    shaders/depth_triangle_ngg_front.sb \
	    shaders/depth_triangle_ngg_front_sb.h
	xxd -i -n depth_triangle_ngg_back_data \
	    shaders/depth_triangle_ngg_back.sb \
	    shaders/depth_triangle_ngg_back_sb.h

# Regenerate a merged gfx1013 VS+GS NGG program and C headers.
shaders_ngg: shaders/triangle_vert.spv shaders/triangle_geom.spv
	$(PSBC) -f shaders/triangle_geom.spv -s geometry \
	    --pre-vertex shaders/triangle_vert.spv \
	    --wave32 \
	    -o shaders/triangle_ngg_back.sb \
	    --ngg-front shaders/triangle_ngg_front.sb \
	    --vertex-attribute 0:0:0:20:r32g32_float \
	    --vertex-attribute 1:0:8:20:r32g32b32_float
	xxd -i -n triangle_ngg_front_data \
	    shaders/triangle_ngg_front.sb shaders/triangle_ngg_front_sb.h
	xxd -i -n triangle_ngg_back_data \
	    shaders/triangle_ngg_back.sb shaders/triangle_ngg_back_sb.h

.PHONY: shaders_tess
shaders_tess: shaders/triangle_tess_vert.spv shaders/triangle_tesc.spv shaders/triangle_tese.spv
	$(PSBC) -f shaders/triangle_tesc.spv -s tess-ctrl \
	    --pre-vertex shaders/triangle_tess_vert.spv \
	    --merged-front shaders/triangle_tess_hs_front.sb \
	    --wave32 \
	    --tess-control-points 3 --tess-patches 8 \
	    -o shaders/triangle_tess_hs_back.sb
	$(PSBC) -f shaders/triangle_tese.spv -s tess-eval \
	    --ngg-front shaders/triangle_tess_es_front.sb \
	    --wave32 \
	    --tess-control-points 3 --tess-patches 8 \
	    -o shaders/triangle_tess_es_back.sb
	xxd -i -n triangle_tess_hs_front_data \
	    shaders/triangle_tess_hs_front.sb \
	    shaders/triangle_tess_hs_front_sb.h
	xxd -i -n triangle_tess_hs_back_data \
	    shaders/triangle_tess_hs_back.sb \
	    shaders/triangle_tess_hs_back_sb.h
	xxd -i -n triangle_tess_es_front_data \
	    shaders/triangle_tess_es_front.sb \
	    shaders/triangle_tess_es_front_sb.h
	xxd -i -n triangle_tess_es_back_data \
	    shaders/triangle_tess_es_back.sb \
	    shaders/triangle_tess_es_back_sb.h

.PHONY: shaders_tess_geometry
shaders_tess_geometry: shaders_tess shaders/triangle_tess_geom.spv
	$(PSBC) -f shaders/triangle_tess_geom.spv -s geometry \
	    --pre-tess-eval shaders/triangle_tese.spv \
	    --ngg-front shaders/triangle_tess_gs_front.sb \
	    --wave32 \
	    --tess-control-points 3 --tess-patches 8 \
	    -o shaders/triangle_tess_gs_back.sb
	xxd -i -n triangle_tess_gs_front_data \
	    shaders/triangle_tess_gs_front.sb \
	    shaders/triangle_tess_gs_front_sb.h
	xxd -i -n triangle_tess_gs_back_data \
	    shaders/triangle_tess_gs_back.sb \
	    shaders/triangle_tess_gs_back_sb.h

.PHONY: shaders_tess_geometry_invocations
shaders_tess_geometry_invocations: shaders_tess shaders/triangle_invocations.geom
	$(GLSLANG) -V --target-env vulkan1.2 shaders/triangle_invocations.geom \
	    -o shaders/triangle_invocations.spv
	$(PSBC) -f shaders/triangle_invocations.spv -s geometry \
	    --pre-tess-eval shaders/triangle_tese.spv \
	    --ngg-front shaders/triangle_tess_invocations_gs_front.sb \
	    --wave32 \
	    --tess-control-points 3 --tess-patches 8 \
	    -o shaders/triangle_tess_invocations_gs_back.sb
	xxd -i -n triangle_tess_invocations_gs_front_data \
	    shaders/triangle_tess_invocations_gs_front.sb \
	    shaders/triangle_tess_invocations_gs_front_sb.h
	xxd -i -n triangle_tess_invocations_gs_back_data \
	    shaders/triangle_tess_invocations_gs_back.sb \
	    shaders/triangle_tess_invocations_gs_back_sb.h

.PHONY: shaders_tess_geometry_lines
shaders_tess_geometry_lines: shaders_tess shaders/triangle_tess_lines.geom
	$(GLSLANG) -V --target-env vulkan1.2 shaders/triangle_tess_lines.geom \
	    -o shaders/triangle_tess_lines.spv
	$(PSBC) -f shaders/triangle_tess_lines.spv -s geometry \
	    --pre-tess-eval shaders/triangle_tese.spv \
	    --ngg-front shaders/triangle_tess_lines_gs_front.sb \
	    --wave32 \
	    --tess-control-points 3 --tess-patches 8 \
	    -o shaders/triangle_tess_lines_gs_back.sb
	xxd -i -n triangle_tess_lines_gs_front_data \
	    shaders/triangle_tess_lines_gs_front.sb \
	    shaders/triangle_tess_lines_gs_front_sb.h
	xxd -i -n triangle_tess_lines_gs_back_data \
	    shaders/triangle_tess_lines_gs_back.sb \
	    shaders/triangle_tess_lines_gs_back_sb.h

.PHONY: shaders_ngg_amplify
shaders_ngg_amplify: shaders/triangle_vert.spv shaders/triangle_amplify.geom
	glslangValidator -V --target-env vulkan1.2 shaders/triangle_amplify.geom \
	    -o shaders/triangle_amplify.spv
	$(PSBC) -f shaders/triangle_amplify.spv -s geometry \
	    --pre-vertex shaders/triangle_vert.spv \
	    --wave32 \
	    -o shaders/triangle_ngg_amplify_back.sb \
	    --ngg-front shaders/triangle_ngg_amplify_front.sb \
	    --vertex-attribute 0:0:0:20:r32g32_float \
	    --vertex-attribute 1:0:8:20:r32g32b32_float
	xxd -i -n triangle_ngg_amplify_front_data \
	    shaders/triangle_ngg_amplify_front.sb \
	    shaders/triangle_ngg_amplify_front_sb.h
	xxd -i -n triangle_ngg_amplify_back_data \
	    shaders/triangle_ngg_amplify_back.sb \
	    shaders/triangle_ngg_amplify_back_sb.h

.PHONY: shaders_ngg_lines
shaders_ngg_lines: shaders/triangle_vert.spv shaders/triangle_lines.geom
	$(GLSLANG) -V --target-env vulkan1.2 shaders/triangle_lines.geom \
	    -o shaders/triangle_lines.spv
	$(PSBC) -f shaders/triangle_lines.spv -s geometry \
	    --pre-vertex shaders/triangle_vert.spv \
	    --wave32 \
	    -o shaders/triangle_ngg_lines_back.sb \
	    --ngg-front shaders/triangle_ngg_lines_front.sb \
	    --vertex-attribute 0:0:0:20:r32g32_float \
	    --vertex-attribute 1:0:8:20:r32g32b32_float
	xxd -i -n triangle_ngg_lines_front_data \
	    shaders/triangle_ngg_lines_front.sb \
	    shaders/triangle_ngg_lines_front_sb.h
	xxd -i -n triangle_ngg_lines_back_data \
	    shaders/triangle_ngg_lines_back.sb \
	    shaders/triangle_ngg_lines_back_sb.h

.PHONY: shaders_ngg_invocations
shaders_ngg_invocations: shaders/triangle_vert.spv shaders/triangle_invocations.geom
	$(GLSLANG) -V --target-env vulkan1.2 shaders/triangle_invocations.geom \
	    -o shaders/triangle_invocations.spv
	$(PSBC) -f shaders/triangle_invocations.spv -s geometry \
	    --pre-vertex shaders/triangle_vert.spv \
	    --wave32 \
	    -o shaders/triangle_ngg_invocations_back.sb \
	    --ngg-front shaders/triangle_ngg_invocations_front.sb \
	    --vertex-attribute 0:0:0:20:r32g32_float \
	    --vertex-attribute 1:0:8:20:r32g32b32_float
	xxd -i -n triangle_ngg_invocations_front_data \
	    shaders/triangle_ngg_invocations_front.sb \
	    shaders/triangle_ngg_invocations_front_sb.h
	xxd -i -n triangle_ngg_invocations_back_data \
	    shaders/triangle_ngg_invocations_back.sb \
	    shaders/triangle_ngg_invocations_back_sb.h

# Graphics draw call test (AGC + NGG/PS shaders + render target + VideoOut)
agc_graphics.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_EXPECT_FIRMWARE_ABI_KEY=0x1160u \
		-DAGC_GRAPHICS_HEADLESS=1 -DAGC_SELF_TERMINATE=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

FW1160_GRAPHICS_FLAGS := -DAGC_EXPECT_FIRMWARE_ABI_KEY=0x1160u \
	-DAGC_GRAPHICS_HEADLESS=1 -DAGC_SELF_TERMINATE=1

agc_graphics_r16_float_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_R16_FLOAT=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rg16_float_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_RG16_FLOAT=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgb10a2_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_RGB10A2=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r11g11b10_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_R11G11B10=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r8_unorm_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_R8_UNORM=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rg8_unorm_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_RG8_UNORM=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r32_float_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_R32_FLOAT=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rg32_float_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_RG32_FLOAT=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgba32_float_fw1160.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_GRAPHICS_FLAGS) -DAGC_VALIDATE_RGBA32_FLOAT=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_indexed.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DRAW_INDEXED=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_indirect.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DRAW_INDIRECT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_indexed_indirect.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DRAW_INDEXED_INDIRECT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_amplify.elf: shaders_ngg_amplify agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_NGG_AMPLIFY=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_lines.elf: shaders_ngg_lines agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_line_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_NGG_INPUT_LINES=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_invocations.elf: shaders_ngg_invocations agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_NGG_INVOCATIONS=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgba8.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RGBA8_REFERENCE=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgba8_std.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RGBA8_STD=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgb10a2.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RGB10A2=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r11g11b10.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_R11G11B10=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgba8_srgb.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RGBA8_SRGB=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_bgra8_srgb.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_BGRA8_SRGB=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r16_float.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_R16_FLOAT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rg16_float.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RG16_FLOAT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r8_unorm.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_R8_UNORM=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rg8_unorm.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RG8_UNORM=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_r32_float.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_R32_FLOAT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rg32_float.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RG32_FLOAT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgba32_float.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RGBA32_FLOAT=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_d16.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_D16_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_stencil_s8.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_STENCIL_VALIDATION=1 -DAGC_S8_ONLY_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_stencil_d16_s8.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_D16_VALIDATION=1 -DAGC_STENCIL_VALIDATION=1 -DAGC_D16_S8_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

FW1160_DEPTH_FLAGS := $(FW1160_GRAPHICS_FLAGS) -DAGC_DEPTH_VALIDATION=1

agc_depth_d16_fw1160.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_DEPTH_FLAGS) -DAGC_D16_VALIDATION=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_stencil_s8_fw1160.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_DEPTH_FLAGS) -DAGC_STENCIL_VALIDATION=1 \
		-DAGC_S8_ONLY_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_stencil_d16_s8_fw1160.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(FW1160_DEPTH_FLAGS) -DAGC_D16_VALIDATION=1 \
		-DAGC_STENCIL_VALIDATION=1 -DAGC_D16_S8_VALIDATION=1 \
		-o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_d16_htile.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_D16_VALIDATION=1 -DAGC_D16_HTILE_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_OPERATION_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_d16_expclear.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_D16_VALIDATION=1 -DAGC_D16_HTILE_VALIDATION=1 -DAGC_D16_HTILE_EXPCLEAR_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_OPERATION_VALIDATION=1 -DAGC_EXPCLEAR_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_stencil.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_STENCIL_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_msaa.elf: shaders_depth shaders/depth_resolve_frag_sb.h agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_MSAA_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_htile.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_htile_ops.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_OPERATION_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_htile_mip.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_MIP_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_htile_array.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_ARRAY_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_expclear.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_OPERATION_VALIDATION=1 -DAGC_EXPCLEAR_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_stencil_htile.elf: shaders_depth agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_DEPTH_VALIDATION=1 -DAGC_STENCIL_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 -DAGC_HTILE_OPERATION_VALIDATION=1 -DAGC_STENCIL_HTILE_VALIDATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

COMBINED_EXPCLEAR_FLAGS := -DAGC_DEPTH_VALIDATION=1 \
	-DAGC_STENCIL_VALIDATION=1 -DAGC_HTILE_VALIDATION=1 \
	-DAGC_HTILE_OPERATION_VALIDATION=1 -DAGC_EXPCLEAR_VALIDATION=1 \
	-DAGC_STENCIL_HTILE_VALIDATION=1 \
	-DAGC_STENCIL_HTILE_EXPCLEAR_VALIDATION=1

agc_depth_stencil_expclear_depth.elf: shaders_depth shaders_htile_rmw agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(COMBINED_EXPCLEAR_FLAGS) -DAGC_EXPCLEAR_ASPECTS=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_stencil_expclear_stencil.elf: shaders_depth shaders_htile_rmw agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(COMBINED_EXPCLEAR_FLAGS) -DAGC_EXPCLEAR_ASPECTS=2 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_depth_stencil_expclear_both.elf: shaders_depth shaders_htile_rmw agc_graphics.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) $(COMBINED_EXPCLEAR_FLAGS) -DAGC_EXPCLEAR_ASPECTS=3 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tessellation.elf: shaders_tess agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry.elf: shaders_tess_geometry agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry_invocations.elf: shaders_tess_geometry_invocations agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -DAGC_TESS_GEOMETRY_INVOCATIONS=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry_lines.elf: shaders_tess_geometry_lines agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_line_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -DAGC_TESS_GEOMETRY_LINES=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry_rgba8.elf: shaders_tess_geometry agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -DAGC_VALIDATE_RGBA8_REFERENCE=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

# === eboot.bin versions (for debug-mode PS5) ===

videoout_linear.bin: videoout_linear.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

agc_init.bin: agc_init.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

agc_videoout.bin: agc_videoout.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

agc_compute.bin: agc_compute.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

agc_graphics.bin: agc_graphics.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

# === .pkg packages (for debug-mode PS5 installation) ===
# Each pkg_* target builds an app folder + .pkg from the corresponding .bin

PKG_OUT := pkg_out

pkg_videoout: videoout_linear.bin
	rm -rf pkg_videoout_build $(PKG_OUT)/videoout
	mkdir -p pkg_videoout_build/sce_sys
	cp videoout_linear.bin pkg_videoout_build/eboot.bin
	cp sce_sys/param.json pkg_videoout_build/sce_sys/
	$(PKGBUILD) pkg_videoout_build $(PKG_OUT)/videoout \
	    $(CONTENT_ID) $(PASSCODE) \
	    --title "openagc VideoOut Linear" --title-id $(TITLE_ID)
	rm -rf pkg_videoout_build

pkg_agc: agc_init.bin
	rm -rf pkg_agc_build $(PKG_OUT)/agc
	mkdir -p pkg_agc_build/sce_sys
	cp agc_init.bin pkg_agc_build/eboot.bin
	cp sce_sys/param.json pkg_agc_build/sce_sys/
	$(PKGBUILD) pkg_agc_build $(PKG_OUT)/agc \
	    $(CONTENT_ID) $(PASSCODE) \
	    --title "openagc AGC Init" --title-id $(TITLE_ID)
	rm -rf pkg_agc_build

pkg_agc_videoout: agc_videoout.bin
	rm -rf pkg_agc_videoout_build $(PKG_OUT)/agc_videoout
	mkdir -p pkg_agc_videoout_build/sce_sys
	cp agc_videoout.bin pkg_agc_videoout_build/eboot.bin
	cp sce_sys/param.json pkg_agc_videoout_build/sce_sys/
	$(PKGBUILD) pkg_agc_videoout_build $(PKG_OUT)/agc_videoout \
	    $(CONTENT_ID) $(PASSCODE) \
	    --title "openagc AGC+VideoOut Test" --title-id $(TITLE_ID)
	rm -rf pkg_agc_videoout_build

pkg_all: pkg_videoout pkg_agc pkg_agc_videoout

# === Deploy (exploited PS5 through etaHEN websrv) ===

deploy_videoout: videoout_linear.elf
	PS5_HOST=$(PS5_HOST) ./run_fw550_conformance.sh --sample videoout_linear

deploy_agc: agc_init.elf
	PS5_HOST=$(PS5_HOST) ./run_fw550_conformance.sh --sample agc_init

.PHONY: deploy_agc_fw1160
deploy_agc_fw1160: agc_init_fw1160.elf
	PS5_HOST=$(PS5_HOST) FULL_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) sh ./run_fw1160_full.sh

.PHONY: deploy_agc_fw1160_stage0 deploy_agc_fw1160_stage1 \
	deploy_agc_fw1160_stage2 deploy_agc_fw1160_stage3 \
	deploy_agc_fw1160_stage4 deploy_agc_fw1160_stage5 \
	deploy_agc_fw1160_stage6 deploy_agc_fw1160_stage7 \
	deploy_agc_fw1160_stage8 deploy_agc_fw1160_stage9 \
	deploy_agc_fw1160_stage10 deploy_agc_fw1160_stage11 \
	deploy_agc_fw1160_stage12 deploy_agc_fw1160_stage13 \
	deploy_agc_fw1160_stage14 deploy_agc_fw1160_stage15 \
	deploy_agc_fw1160_stage16 deploy_agc_fw1160_stage17
deploy_agc_fw1160_stage0: agc_fw1160_stage0.elf
	PS5_HOST=$(PS5_HOST) STAGE=0 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage1: agc_fw1160_stage1.elf
	PS5_HOST=$(PS5_HOST) STAGE=1 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage2: agc_fw1160_stage2.elf
	PS5_HOST=$(PS5_HOST) STAGE=2 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage3: agc_fw1160_stage3.elf
	PS5_HOST=$(PS5_HOST) STAGE=3 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage4: agc_fw1160_stage4.elf
	PS5_HOST=$(PS5_HOST) STAGE=4 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage5: agc_fw1160_stage5.elf
	PS5_HOST=$(PS5_HOST) STAGE=5 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage6: agc_fw1160_stage6.elf
	PS5_HOST=$(PS5_HOST) STAGE=6 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage7: agc_fw1160_stage7.elf
	PS5_HOST=$(PS5_HOST) STAGE=7 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage8: agc_fw1160_stage8.elf
	PS5_HOST=$(PS5_HOST) STAGE=8 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage9: agc_fw1160_stage9.elf
	PS5_HOST=$(PS5_HOST) STAGE=9 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage10: agc_fw1160_stage10.elf
	PS5_HOST=$(PS5_HOST) STAGE=10 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage11: agc_fw1160_stage11.elf
	PS5_HOST=$(PS5_HOST) STAGE=11 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage12: agc_fw1160_stage12.elf
	PS5_HOST=$(PS5_HOST) STAGE=12 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage13: agc_fw1160_stage13.elf
	PS5_HOST=$(PS5_HOST) STAGE=13 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage14: agc_fw1160_stage14.elf
	PS5_HOST=$(PS5_HOST) STAGE=14 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage15: agc_fw1160_stage15.elf
	PS5_HOST=$(PS5_HOST) STAGE=15 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage16: agc_fw1160_stage16.elf
	PS5_HOST=$(PS5_HOST) STAGE=16 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

deploy_agc_fw1160_stage17: agc_fw1160_stage17.elf
	PS5_HOST=$(PS5_HOST) STAGE=17 STAGE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) ./run_fw1160_stage.sh

.PHONY: deploy_agc_fw1160_sony_workload
deploy_agc_fw1160_sony_workload: fw1160_sony_workload_check
	PS5_HOST=$(PS5_HOST) ORACLE_ARTIFACT=$(THIS_DIR)/agc_fw1160_sony_workload.elf \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		ALLOW_SONY_DRIVER_ORACLE=$(ALLOW_SONY_DRIVER_ORACLE) \
		./run_fw1160_sony_workload.sh

deploy_agc_videoout: agc_videoout.elf
	PS5_HOST=$(PS5_HOST) ./run_fw550_conformance.sh --sample agc_videoout

deploy_agc_compute: agc_compute.elf
	PS5_HOST=$(PS5_HOST) ./run_fw550_conformance.sh --sample agc_compute

.PHONY: deploy_agc_compute_fw1160
deploy_agc_compute_fw1160: agc_compute_fw1160.elf
	PS5_HOST=$(PS5_HOST) COMPUTE_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) sh ./run_fw1160_compute.sh

deploy_agc_graphics: agc_graphics.elf
	PS5_HOST=$(PS5_HOST) ./run_fw550_conformance.sh --sample agc_graphics

.PHONY: deploy_agc_graphics_fw1160 \
	deploy_agc_graphics_r16_float_fw1160 \
	deploy_agc_graphics_rg16_float_fw1160 \
	deploy_agc_graphics_rgb10a2_fw1160 \
	deploy_agc_graphics_r11g11b10_fw1160 \
	deploy_agc_graphics_r8_unorm_fw1160 \
	deploy_agc_graphics_rg8_unorm_fw1160 \
	deploy_agc_graphics_r32_float_fw1160 \
	deploy_agc_graphics_rg32_float_fw1160 \
	deploy_agc_graphics_rgba32_float_fw1160
deploy_agc_graphics_fw1160: agc_graphics_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) sh ./run_fw1160_graphics.sh

deploy_agc_graphics_r16_float_fw1160: agc_graphics_r16_float_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=R16_FLOAT PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_rg16_float_fw1160: agc_graphics_rg16_float_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=RG16_FLOAT PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_rgb10a2_fw1160: agc_graphics_rgb10a2_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET='offscreen RGB10A2' PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_r11g11b10_fw1160: agc_graphics_r11g11b10_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET='offscreen R11G11B10 FLOAT' PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_r8_unorm_fw1160: agc_graphics_r8_unorm_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=R8_UNORM PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_rg8_unorm_fw1160: agc_graphics_rg8_unorm_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=RG8_UNORM PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_r32_float_fw1160: agc_graphics_r32_float_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=R32_FLOAT PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_rg32_float_fw1160: agc_graphics_rg32_float_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=RG32_FLOAT PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

deploy_agc_graphics_rgba32_float_fw1160: agc_graphics_rgba32_float_fw1160.elf
	PS5_HOST=$(PS5_HOST) GRAPHICS_ARTIFACT=$(THIS_DIR)/$< \
		EXPECTED_TARGET=RGBA32_FLOAT PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) \
		sh ./run_fw1160_graphics.sh

.PHONY: deploy_agc_depth_d16_fw1160 deploy_agc_stencil_s8_fw1160 \
	deploy_agc_depth_stencil_d16_s8_fw1160
deploy_agc_depth_d16_fw1160: agc_depth_d16_fw1160.elf
	PS5_HOST=$(PS5_HOST) DEPTH_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) sh ./run_fw1160_depth.sh

deploy_agc_stencil_s8_fw1160: agc_stencil_s8_fw1160.elf
	PS5_HOST=$(PS5_HOST) DEPTH_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) sh ./run_fw1160_depth.sh

deploy_agc_depth_stencil_d16_s8_fw1160: agc_depth_stencil_d16_s8_fw1160.elf
	PS5_HOST=$(PS5_HOST) DEPTH_ARTIFACT=$(THIS_DIR)/$< \
		PROCESS_CLEANUP_ELF=$(PROCESS_CLEANUP_ELF) sh ./run_fw1160_depth.sh

deploy_agc_depth: agc_depth.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth/sce_sys"
	curl -T agc_depth.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth/eboot.elf"

deploy_agc_depth_stencil: agc_depth_stencil.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_stencil"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_stencil/sce_sys"
	curl -T agc_depth_stencil.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_stencil/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_stencil/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_stencil/eboot.elf"

# === Install (debug-mode PS5 via FTP) ===

install_videoout: videoout_linear.bin
	curl -Q "MKD /system_ex/app/$(TITLE_ID)"         "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /system_ex/app/$(TITLE_ID)/sce_sys" "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /user/app/$(TITLE_ID)"              "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /user/app/$(TITLE_ID)/sce_sys"      "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -T "videoout_linear.bin"      "ftp://$(PS5_HOST):2121/system_ex/app/$(TITLE_ID)/eboot.bin"
	curl -T "sce_sys/param.json"       "ftp://$(PS5_HOST):2121/user/app/$(TITLE_ID)/sce_sys/"

install_agc: agc_init.bin
	curl -Q "MKD /system_ex/app/$(TITLE_ID)"         "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /system_ex/app/$(TITLE_ID)/sce_sys" "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /user/app/$(TITLE_ID)"              "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /user/app/$(TITLE_ID)/sce_sys"      "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -T "agc_init.bin"            "ftp://$(PS5_HOST):2121/system_ex/app/$(TITLE_ID)/eboot.bin"
	curl -T "sce_sys/param.json"       "ftp://$(PS5_HOST):2121/user/app/$(TITLE_ID)/sce_sys/"

install_agc_videoout: agc_videoout.bin
	curl -Q "MKD /system_ex/app/$(TITLE_ID)"         "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /system_ex/app/$(TITLE_ID)/sce_sys" "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /user/app/$(TITLE_ID)"              "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -Q "MKD /user/app/$(TITLE_ID)/sce_sys"      "ftp://$(PS5_HOST):2121/" > /dev/null || true
	curl -T "agc_videoout.bin"        "ftp://$(PS5_HOST):2121/system_ex/app/$(TITLE_ID)/eboot.bin"
	curl -T "sce_sys/param.json"       "ftp://$(PS5_HOST):2121/user/app/$(TITLE_ID)/sce_sys/"

# === Debug ===

# === Emulator builds (KytyPS5 / SharpEmu) ===
# These use a custom _start (crt_emu.c) that skips the ps5-payload-sdk CRT
# bootstrap, which expects payload_args_t* with sys_dynlib_dlsym.
# Emulators pass different args to _start, so we bypass the CRT entirely.
EMU_CFLAGS  := $(CFLAGS) -Wno-unused-command-line-argument
EMU_LDFLAGS := -nostartfiles -lc -lkernel

emu_videoout: videoout_linear.c ps5_video_out.h crt_emu.c
	$(CC) $(EMU_CFLAGS) -o emu_videoout.elf crt_emu.c videoout_linear.c $(EMU_LDFLAGS) -lSceVideoOut

emu_agc: agc_init.c ps5_video_out.h crt_emu.c $(OPENAGC_LIB)
	$(CC) $(EMU_CFLAGS) -o emu_agc.elf crt_emu.c agc_init.c $(EMU_LDFLAGS) $(OPENAGC_LIB) -lSceVideoOut

emu_agc_videoout: agc_videoout.c ps5_video_out.h crt_emu.c $(OPENAGC_LIB)
	$(CC) $(EMU_CFLAGS) -o emu_agc_videoout.elf crt_emu.c agc_videoout.c $(EMU_LDFLAGS) $(OPENAGC_LIB) -lSceVideoOut

emu_all: emu_videoout emu_agc emu_agc_videoout

# SharpEmu HLE test — calls sceAgc* NID exports directly (no /dev/gc backend)
# This test works with SharpEmu because it uses NID-based imports that
# SharpEmu's HLE resolves. It does NOT use the prospero /dev/gc driver.
emu_sharpemu: emu_sharpemu.c crt_emu.c
	$(CC) $(EMU_CFLAGS) -o emu_sharpemu.elf crt_emu.c emu_sharpemu.c $(EMU_LDFLAGS) -lSceAgc -lSceAgcDriver

# Build eboot.bin from emulator ELF for SharpEmu/KytyPS5
emu_videoout.bin: emu_videoout.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

emu_agc.bin: emu_agc.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

emu_agc_videoout.bin: emu_agc_videoout.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

emu_sharpemu.bin: emu_sharpemu.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

debug_videoout: videoout_linear.elf
	gdb-multiarch \
	    -ex "set architecture i386:x86-64" \
	    -ex "target extended-remote $(PS5_HOST):2159" \
	    -ex "file $<" \
	    -ex "remote put $< /data/$<" \
	    -ex "set remote exec-file /data/$<" \
	    -ex "start"

.PHONY: deploy_agc_depth_msaa
deploy_agc_depth_msaa: agc_depth_msaa.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_msaa"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_msaa/sce_sys"
	curl -T agc_depth_msaa.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_msaa/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_msaa/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_msaa/eboot.elf"

.PHONY: deploy_agc_depth_htile
deploy_agc_depth_htile: agc_depth_htile.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_htile"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_htile/sce_sys"
	curl -T agc_depth_htile.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_htile/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_htile/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_htile/eboot.elf"

.PHONY: deploy_agc_depth_htile_ops
deploy_agc_depth_htile_ops: agc_depth_htile_ops.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_htile_ops"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_htile_ops/sce_sys"
	curl -T agc_depth_htile_ops.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_htile_ops/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_htile_ops/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_htile_ops/eboot.elf"

.PHONY: deploy_agc_depth_expclear
deploy_agc_depth_expclear: agc_depth_expclear.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_expclear"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_expclear/sce_sys"
	curl -T agc_depth_expclear.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_expclear/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_expclear/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_expclear/eboot.elf"

.PHONY: deploy_agc_depth_d16_expclear
deploy_agc_depth_d16_expclear: agc_depth_d16_expclear.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_d16_expclear"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_d16_expclear/sce_sys"
	curl -T agc_depth_d16_expclear.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_d16_expclear/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_d16_expclear/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_d16_expclear/eboot.elf"

.PHONY: deploy_agc_depth_stencil_htile
deploy_agc_depth_stencil_htile: agc_depth_stencil_htile.elf
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_stencil_htile"
	-curl -s "ftp://$(PS5_HOST):2121/" --quote "MKD /data/homebrew/agc_depth_stencil_htile/sce_sys"
	curl -T agc_depth_stencil_htile.elf "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_stencil_htile/eboot.elf"
	curl -T sce_sys/icon0.png "ftp://$(PS5_HOST):2121/data/homebrew/agc_depth_stencil_htile/sce_sys/icon0.png"
	curl -s "http://$(PS5_HOST):8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth_stencil_htile/eboot.elf"

clean:
	rm -f *.elf *.bin
	rm -rf pkg_*_build pkg_out pkg_build
