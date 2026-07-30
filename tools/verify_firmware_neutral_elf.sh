#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

# Reject firmware-pinned graphics payloads before their bytes are preserved for
# cross-firmware replay. The result path is supplied by the caller so each gate
# can keep a stale-proof file without embedding a firmware key in the ELF.

set -eu

artifact=${1:?usage: verify_firmware_neutral_elf.sh artifact expected-result-path}
result_path=${2:?usage: verify_firmware_neutral_elf.sh artifact expected-result-path}
readelf_tool=${READELF:-llvm-readelf}

if [ ! -s "$artifact" ]; then
    echo "missing firmware-neutral ELF: $artifact" >&2
    exit 1
fi
if "$readelf_tool" -d "$artifact" | grep 'NEEDED' | \
        grep -Eq 'libSceAgc(Driver)?\.sprx'; then
    echo "firmware-neutral ELF links an AGC SPRX" >&2
    exit 1
fi
if ! strings "$artifact" | grep -Fqx "$result_path"; then
    echo "firmware-neutral ELF is missing result path: $result_path" >&2
    exit 1
fi
if strings "$artifact" | grep -Eq \
        'openagc_fw(550|1160)|AGC_EXPECT_FIRMWARE_ABI_KEY'; then
    echo "firmware-neutral ELF contains a firmware-specific oracle" >&2
    exit 1
fi

echo "PASS: firmware-neutral ELF has no firmware expectation or AGC SPRX dependency"
