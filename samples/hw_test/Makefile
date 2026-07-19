# openagc hardware validation samples — Makefile
#
# Two test programs adapted from freegnm-examples:
#   videoout_linear.elf — VideoOut display pipeline smoke test (no GPU)
#   agc_init.elf        — AGC init + NOP submit + queue test (GPU commands)
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

# LibProsperoPkg tools (for eboot.bin)
PKG_TOOLS ?= /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build
FSELF     := $(PKG_TOOLS)/prosperopkg-fself

PS4_SDK_VERSION ?= 0x07590001

CFLAGS  := -Wall -Wextra -g -I$(OPENAGC_INC) -I$(THIS_DIR)

# videoout_linear: no AGC dependency, just VideoOut + kernel
VIDEOOUT_LDFLAGS := -lSceVideoOut -lkernel

# agc_init: links libopenagc.a + VideoOut + kernel
AGC_LDFLAGS := $(OPENAGC_LIB) -lSceVideoOut -lkernel

# === Targets ===

all: videoout_linear.elf agc_init.elf

# VideoOut linear smoke test (no GPU commands)
videoout_linear.elf: videoout_linear.c ps5_video_out.h
	$(CC) $(CFLAGS) -o $@ videoout_linear.c $(VIDEOOUT_LDFLAGS)

# AGC init + NOP submit test (links libopenagc.a)
agc_init.elf: agc_init.c ps5_video_out.h $(OPENAGC_LIB)
	$(CC) $(CFLAGS) -o $@ agc_init.c $(AGC_LDFLAGS)

# === eboot.bin versions (for debug-mode PS5) ===

videoout_linear.bin: videoout_linear.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

agc_init.bin: agc_init.elf
	$(FSELF) $< $@ $(PS4_SDK_VERSION) $(PS4_SDK_VERSION)

# === Deploy (exploited PS5 via ELF loader) ===

deploy_videoout: videoout_linear.elf
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

deploy_agc: agc_init.elf
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

# === Debug ===

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
