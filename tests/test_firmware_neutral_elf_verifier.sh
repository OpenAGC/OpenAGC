#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

verifier=${1:?usage: test_firmware_neutral_elf_verifier.sh verifier}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
result_path=/data/homebrew/openagc_portable_graphics/result.log
printf '#!/bin/sh\nexit 0\n' > "$tmp/readelf"
chmod +x "$tmp/readelf"

printf 'neutral payload\n%s\n' "$result_path" > "$tmp/neutral.elf"
READELF="$tmp/readelf" "$verifier" \
    "$tmp/neutral.elf" "$result_path" >/dev/null

cp "$tmp/neutral.elf" "$tmp/pinned.elf"
printf '\000AGC_EXPECT_FIRMWARE_ABI_KEY\000' >> "$tmp/pinned.elf"
if READELF="$tmp/readelf" "$verifier" \
        "$tmp/pinned.elf" "$result_path" >/dev/null 2>&1; then
    echo "neutral verifier accepted a firmware expectation" >&2
    exit 1
fi

if READELF="$tmp/readelf" "$verifier" \
        "$tmp/neutral.elf" /wrong/result.log >/dev/null 2>&1; then
    echo "neutral verifier accepted the wrong result path" >&2
    exit 1
fi

echo "PASS: firmware-neutral verifier rejects pinned and stale-path ELFs"
