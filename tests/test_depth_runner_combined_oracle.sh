#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

runner=${1:?usage: test_depth_runner_combined_oracle.sh run-depth-script}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

printf 'combined depth bytes\n' > "$tmp/depth.elf"
printf 'cleanup bytes\n' > "$tmp/cleanup.elf"
cat > "$tmp/result.log" <<'RESULT'
Runtime profile FW ABI 0x1160: PASS
GPU completion fence reached
[MSAA] shader-resolved 4x RGBA8 to 1x headless target
[Combined Expclear RMW] aspects=0x1 gate=ON offset=0x0 size=0x1000 selected=1024 expected=fffffff0 mismatch=0 outside-changed=0 reserved=PASS fence=48544c45: PASS
[HTILE Readback] changed=1024 other=0 initial=fffff30f
[HTILE Subresource Readback] selected-changed=1024 outside-changed=0
[Depth Readback] green=228096 red=228096 left=ff00ff00 right=ffff0000
[Depth Readback] raw D32: one=1755648 near=228096 far=228096
[Stencil Readback] zero=2165248 replace-5a=456192 other=0
[Sample Rate] mode=full-4x samples=10,11,12,13 total=46 guards=deadbeef,deadbeef,deadbeef,deadbeef: PASS
[Depth+Stencil Result] markers=PASS color=PASS raw-depth=PASS stencil=PASS
[Depth+4xMSAA Result] markers=PASS color=PASS raw-depth=PASS stencil=PASS
Driver shutdown: PASS
Graphics result: PASS
RESULT

mkdir -p "$tmp/bin"
cat > "$tmp/bin/curl" <<'MOCK_CURL'
#!/bin/sh
set -eu
output=
cleanup_http=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        http://*/process_cleanup/eboot.elf*) cleanup_http=1 ;;
    esac
    if [ "$1" = "-o" ]; then
        output=$2
        shift 2
    else
        shift
    fi
done
if [ "${MOCK_CLEANUP_TIMEOUT:-0}" -eq 1 ] && [ "$cleanup_http" -eq 1 ]; then
    exit 28
fi
if [ -n "$output" ]; then
    cp "$MOCK_RESULT" "$output"
fi
MOCK_CURL
cat > "$tmp/bin/sleep" <<'MOCK_SLEEP'
#!/bin/sh
exit 0
MOCK_SLEEP
chmod +x "$tmp/bin/curl" "$tmp/bin/sleep"

if PATH="$tmp/bin:$PATH" PS5_HOST=mock \
    DEPTH_ARTIFACT="$tmp/depth.elf" \
    PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" \
    EXPECTED_ARTIFACT_SHA256=0000000000000000000000000000000000000000000000000000000000000000 \
    sh "$runner" > "$tmp/hash-output" 2>&1; then
    echo "depth runner accepted changed artifact bytes" >&2
    exit 1
fi
grep -q '^depth artifact SHA-256 mismatch$' "$tmp/hash-output"

run_gate()
{
    PATH="$tmp/bin:$PATH" \
        MOCK_CLEANUP_TIMEOUT=1 PS5_HOST=mock \
        DEPTH_ARTIFACT="$tmp/depth.elf" \
        PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" \
        RESULT_LOG_PATH=/data/homebrew/openagc_fw1160_depth/result.log \
        EXPECTED_HTILE_INITIAL=fffff30f EXPECTED_D32_FULL_RECT=1 \
        EXPECTED_D32_ONE_COUNT="$2" \
        EXPECTED_D32_NEAR_COUNT="$6" \
        EXPECTED_D32_FAR_COUNT="$7" \
        EXPECTED_HTILE_CHANGED=1024 \
        REQUIRE_HTILE_SUBRESOURCE=1 \
        EXPECTED_HTILE_SELECTED_CHANGED="$3" \
        EXPECTED_HTILE_OUTSIDE_CHANGED="$4" \
        EXPECTED_COLOR_GREEN_RED="$5" \
        REQUIRE_MSAA_RESOLVE=1 \
        MOCK_RESULT="$8" \
        EXPECTED_STENCIL_FULL_RECT=1 \
        EXPECTED_COMBINED_EXPCLEAR_ASPECTS="$1" \
        sh "$runner"
}

run_msaa_gate()
{
    PATH="$tmp/bin:$PATH" \
        MOCK_CLEANUP_TIMEOUT=1 PS5_HOST=mock \
        DEPTH_ARTIFACT="$tmp/depth.elf" \
        PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" \
        RESULT_LOG_PATH=/data/homebrew/openagc_fw1160_depth/result.log \
        REQUIRE_MSAA_RESOLVE=1 \
        REQUIRE_SAMPLE_RATE_MODE="${2:-}" \
        EXPECTED_SAMPLE_RATE_COUNTS="${3:-}" \
        MOCK_RESULT="$1" \
        sh "$runner"
}

run_gate 1 1755648 1024 0 228096 228096 228096 "$tmp/result.log" > "$tmp/pass-output"
if run_gate 1 1617408 1024 0 228096 228096 228096 "$tmp/result.log" > "$tmp/count-fail-output" 2>&1; then
    echo "depth runner accepted the wrong allocation-aware D32 count" >&2
    exit 1
fi
if run_gate 2 1755648 1024 0 228096 228096 228096 "$tmp/result.log" > "$tmp/fail-output" 2>&1; then
    echo "depth runner accepted the wrong combined expclear aspect" >&2
    exit 1
fi
if run_gate 1 1755648 1023 0 228096 228096 228096 "$tmp/result.log" > "$tmp/selected-fail-output" 2>&1; then
    echo "depth runner accepted the wrong selected HTILE count" >&2
    exit 1
fi
if run_gate 1 1755648 1024 1 228096 228096 228096 "$tmp/result.log" > "$tmp/outside-fail-output" 2>&1; then
    echo "depth runner accepted the wrong outside HTILE count" >&2
    exit 1
fi
if run_gate 1 1755648 1024 0 31968 228096 228096 "$tmp/result.log" > "$tmp/color-fail-output" 2>&1; then
    echo "depth runner accepted the wrong depth color count" >&2
    exit 1
fi
if run_gate 1 1755648 1024 0 228096 228095 228096 "$tmp/result.log" > "$tmp/depth-fail-output" 2>&1; then
    echo "depth runner accepted the wrong exact D32 class count" >&2
    exit 1
fi
grep -v '^\[MSAA\]' "$tmp/result.log" > "$tmp/no-msaa.log"
if run_gate 1 1755648 1024 0 228096 228096 228096 "$tmp/no-msaa.log" > "$tmp/msaa-fail-output" 2>&1; then
    echo "depth runner accepted a missing MSAA resolve verdict" >&2
    exit 1
fi

grep -v '^\[Depth+Stencil Result\]' "$tmp/result.log" > "$tmp/msaa-only.log"
run_msaa_gate "$tmp/msaa-only.log" > "$tmp/msaa-only-output"
run_msaa_gate "$tmp/msaa-only.log" full-4x 10,11,12,13,46 > "$tmp/sample-rate-output"
if run_msaa_gate "$tmp/msaa-only.log" full-4x 10,11,12,13,45 > "$tmp/sample-rate-count-fail-output" 2>&1; then
    echo "depth runner accepted wrong exact sample-rate counts" >&2
    exit 1
fi
grep -v '^\[Sample Rate\]' "$tmp/msaa-only.log" > "$tmp/no-sample-rate.log"
if run_msaa_gate "$tmp/no-sample-rate.log" full-4x > "$tmp/sample-rate-fail-output" 2>&1; then
    echo "depth runner accepted a missing sample-rate verdict" >&2
    exit 1
fi

echo "PASS: depth runner enforces exact combined depth/stencil verdicts"
