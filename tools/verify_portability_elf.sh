#!/bin/sh
# Verify that the pinned portability payload has no firmware-SPRX dependency
# or firmware-specific test oracle embedded in its active image.

set -eu

artifact=${1:?usage: verify_portability_elf.sh path/to/agc_portability.elf}
readelf_tool=${READELF:-llvm-readelf}

if [ ! -s "$artifact" ]; then
    echo "missing portability ELF: $artifact" >&2
    exit 1
fi
if "$readelf_tool" -d "$artifact" | grep 'NEEDED' | \
        grep -Eq 'libSceAgc(Driver)?\.sprx'; then
    echo "portability ELF links a firmware AGC SPRX" >&2
    exit 1
fi
if ! strings "$artifact" | grep -q \
        '^/data/homebrew/openagc_portability/result\.log$'; then
    echo "portability ELF is missing its neutral result path" >&2
    exit 1
fi
if strings "$artifact" | grep -Eq \
        'openagc_fw(550|1160)|AGC_EXPECT_FIRMWARE_ABI_KEY'; then
    echo "portability ELF contains a firmware-specific oracle" >&2
    exit 1
fi

echo "PASS: portability ELF has no firmware expectation or AGC SPRX dependency"
