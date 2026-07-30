#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

runner=${1:?usage: test_graphics_runner_fail_closed.sh run-graphics-script}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

printf 'portable graphics bytes\n' > "$tmp/graphics.elf"
printf 'cleanup bytes\n' > "$tmp/cleanup.elf"
mkdir -p "$tmp/bin"
cat > "$tmp/bin/curl" <<'MOCK_CURL'
#!/bin/sh
echo "curl must not run before the local hash gate" >&2
exit 99
MOCK_CURL
chmod +x "$tmp/bin/curl"

if PATH="$tmp/bin:$PATH" PS5_HOST=mock \
    GRAPHICS_ARTIFACT="$tmp/graphics.elf" \
    PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" \
    EXPECTED_ARTIFACT_SHA256=0000000000000000000000000000000000000000000000000000000000000000 \
    sh "$runner" >"$tmp/output" 2>&1; then
    echo "graphics runner accepted an artifact with the wrong hash" >&2
    exit 1
fi
grep -q '^graphics artifact SHA-256 mismatch$' "$tmp/output"
if grep -q 'curl must not run' "$tmp/output"; then
    echo "graphics runner contacted the console before validating bytes" >&2
    exit 1
fi

echo "PASS: graphics runner rejects changed bytes before console access"
