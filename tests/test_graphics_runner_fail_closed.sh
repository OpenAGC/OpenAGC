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

cat > "$tmp/bin/curl" <<'MOCK_CURL'
#!/bin/sh
output=
url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -o)
            output=$2
            shift 2
            ;;
        ftp://*|http://*)
            url=$1
            shift
            ;;
        *)
            shift
            ;;
    esac
done
if [ -n "$output" ]; then
    case "$url" in
        */result.log)
            printf '%s\n' \
                'FATAL: completed graphics validation failed' \
                'Graphics memory cleanup: pool=0x00000000' > "$output"
            ;;
        */eboot.elf)
            cp "$MOCK_GRAPHICS" "$output"
            ;;
    esac
fi
exit 0
MOCK_CURL
chmod +x "$tmp/bin/curl"

artifact_sha=$(shasum -a 256 "$tmp/graphics.elf" | awk '{print $1}')
if PATH="$tmp/bin:$PATH" MOCK_GRAPHICS="$tmp/graphics.elf" PS5_HOST=mock \
    GRAPHICS_ARTIFACT="$tmp/graphics.elf" \
    PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" \
    EXPECTED_ARTIFACT_SHA256="$artifact_sha" \
    RESULT_LOG_PATH=/data/homebrew/openagc_portable_graphics/result.log \
    sh "$runner" >"$tmp/output" 2>&1; then
    echo "graphics runner accepted a completed fatal verdict" >&2
    exit 1
fi
grep -q '^FATAL: completed graphics validation failed$' "$tmp/output"
if grep -q 'timed out waiting for file-backed graphics verdict' "$tmp/output"; then
    echo "graphics runner hid a completed fatal verdict behind a timeout" >&2
    exit 1
fi

echo "PASS: graphics runner surfaces completed fatal verdicts"
