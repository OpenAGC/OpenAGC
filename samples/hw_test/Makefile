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

PS4_SDK_VERSION ?= 0x07590001

# PS5 packaging constants
CONTENT_ID  := UP9000-AGCT00001_00-OPENAGC000000000
PASSCODE    := 00000000000000000000000000000000

CFLAGS  := -Wall -Wextra -g -I$(OPENAGC_INC) -I$(THIS_DIR) $(EXTRA_CFLAGS)

# videoout_linear: no AGC dependency, just VideoOut + kernel
VIDEOOUT_LDFLAGS := -lSceVideoOut -lkernel

# agc_init: links libopenagc.a + VideoOut + kernel
AGC_LDFLAGS := $(OPENAGC_LIB) -lSceVideoOut -lkernel

# === Targets ===

all: videoout_linear.elf agc_init.elf agc_videoout.elf agc_compute.elf agc_graphics.elf agc_tessellation.elf agc_tess_geometry.elf agc_tess_geometry_invocations.elf agc_tess_geometry_lines.elf

# VideoOut linear smoke test (no GPU commands)
videoout_linear.elf: videoout_linear.c ps5_video_out.h
	$(CC) $(CFLAGS) -o $@ videoout_linear.c $(VIDEOOUT_LDFLAGS)

# AGC init + NOP submit test (links libopenagc.a)
agc_init.elf: agc_init.c ps5_video_out.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_init.c $(AGC_LDFLAGS)

# Combined AGC + VideoOut test (links libopenagc.a + VideoOut + kernel)
agc_videoout.elf: agc_videoout.c ps5_video_out.h gpu_credentials.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_videoout.c $(AGC_LDFLAGS)

# Compute dispatch test (AGC + compute shader + VideoOut)
agc_compute.elf: agc_compute.c ps5_video_out.h gpu_credentials.h shaders/fill_color_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_compute.c $(AGC_LDFLAGS)

# Regenerate SPIR-V whenever the editable vertex shader changes.
shaders/triangle_vert.spv: shaders/triangle.vert
	$(GLSLANG) -V --target-env vulkan1.2 $< -o $@

shaders/triangle_frag.spv: shaders/triangle.frag
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

agc_graphics_amplify.elf: shaders_ngg_amplify agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_NGG_AMPLIFY=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_lines.elf: shaders_ngg_lines agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_NGG_INPUT_LINES=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_invocations.elf: shaders_ngg_invocations agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_NGG_INVOCATIONS=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_graphics_rgba8.elf: agc_graphics.c ps5_video_out.h gpu_credentials.h shaders/triangle_ngg_front_sb.h shaders/triangle_ngg_back_sb.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_VALIDATE_RGBA8_REFERENCE=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tessellation.elf: shaders_tess agc_graphics.c gfx1013_tess_ring.h ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry.elf: shaders_tess_geometry agc_graphics.c gfx1013_tess_ring.h ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry_invocations.elf: shaders_tess_geometry_invocations agc_graphics.c gfx1013_tess_ring.h ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -DAGC_TESS_GEOMETRY_INVOCATIONS=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

agc_tess_geometry_lines.elf: shaders_tess_geometry_lines agc_graphics.c gfx1013_tess_ring.h ps5_video_out.h gpu_credentials.h shaders/triangle_frag_sb.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -DAGC_TESSELLATION=1 -DAGC_TESS_GEOMETRY=1 -DAGC_TESS_GEOMETRY_LINES=1 -o $@ agc_graphics.c $(AGC_LDFLAGS)

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

# === Deploy (exploited PS5 via ELF loader) ===

deploy_videoout: videoout_linear.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

deploy_agc: agc_init.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

deploy_agc_videoout: agc_videoout.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

deploy_agc_compute: agc_compute.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

deploy_agc_graphics: agc_graphics.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

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
EMU_CFLAGS  := $(CFLAGS) -nostartfiles
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

clean:
	rm -f *.elf *.bin
	rm -rf pkg_*_build pkg_out pkg_build
