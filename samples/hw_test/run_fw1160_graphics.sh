#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
GRAPHICS_ARTIFACT=${GRAPHICS_ARTIFACT:?set GRAPHICS_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_TARGET=${EXPECTED_TARGET:-offscreen FP16}
EXPECTED_DRAW_MODE=${EXPECTED_DRAW_MODE:-}
EXPECTED_PACKET_AUDIT=${EXPECTED_PACKET_AUDIT:-}
REQUIRE_MULTI_DRAW_ORACLE=${REQUIRE_MULTI_DRAW_ORACLE:-0}
REQUIRE_COUNT_BUFFER_ORACLE=${REQUIRE_COUNT_BUFFER_ORACLE:-0}
EXPECTED_VARIANT=${EXPECTED_VARIANT:-}
REQUIRE_TESS_RINGS=${REQUIRE_TESS_RINGS:-0}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:-0x1160}
EXPECTED_ARTIFACT_SHA256=${EXPECTED_ARTIFACT_SHA256:-}
REMOTE_BASE=/data/homebrew/openagc_fw${EXPECTED_FW_ABI#0x}_graphics

if [ ! -s "$GRAPHICS_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing graphics-test or process-cleanup ELF" >&2
    exit 2
fi
if [ -n "$EXPECTED_ARTIFACT_SHA256" ]; then
    artifact_sha=$(shasum -a 256 "$GRAPHICS_ARTIFACT" | awk '{print $1}') ||
        exit 2
    if [ "$artifact_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
        echo "graphics artifact SHA-256 mismatch" >&2
        echo "expected: $EXPECTED_ARTIFACT_SHA256" >&2
        echo "actual:   $artifact_sha" >&2
        exit 2
    fi
fi

curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "ftp://$PS5_HOST:2121/data/homebrew/process_cleanup/eboot.elf" || exit 1
# The cleanup helper intentionally SIGKILLs itself, so a foreground pipe can
# remain open until curl's timeout even after cleanup has completed. Launch it
# detached, then require websrv to respond before uploading the graphics ELF.
curl -sS --fail --max-time 10 \
    "http://$PS5_HOST:8080/hbldr?pipe=0&daemon=1&path=/data/homebrew/process_cleanup/eboot.elf" \
    >/dev/null || exit 1
sleep 2
curl -sS --fail --max-time 5 "http://$PS5_HOST:8080/" >/dev/null || exit 1

curl -sS --fail --ftp-create-dirs -T "$GRAPHICS_ARTIFACT" \
    "ftp://$PS5_HOST:2121$REMOTE_BASE/eboot.elf" || exit 1
output_file=$(mktemp) || exit 2
uploaded_file=$(mktemp) || exit 2
trap 'rm -f "$output_file" "$uploaded_file"' EXIT HUP INT TERM
if [ -n "$EXPECTED_ARTIFACT_SHA256" ]; then
    curl -sS --fail "ftp://$PS5_HOST:2121$REMOTE_BASE/eboot.elf" \
        -o "$uploaded_file" || exit 1
    uploaded_sha=$(shasum -a 256 "$uploaded_file" | awk '{print $1}') ||
        exit 1
    if [ "$uploaded_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
        echo "uploaded graphics artifact SHA-256 mismatch" >&2
        exit 1
    fi
    echo "Pinned graphics ELF: $EXPECTED_ARTIFACT_SHA256"
fi
transport_status=0
if [ -n "${RESULT_LOG_PATH:-}" ]; then
    result_ftp_url="ftp://$PS5_HOST:2121$RESULT_LOG_PATH"
    result_dir=${RESULT_LOG_PATH%/*}
    # The firmware-neutral artifact can log outside REMOTE_BASE. Ensure its
    # stale-proof verdict directory exists before launch; FTP reports an error
    # when MKD names an existing directory, so that response is intentionally
    # ignored.
    curl -sS --max-time 5 --quote "MKD $result_dir" \
        "ftp://$PS5_HOST:2121/" >/dev/null 2>&1 || true
    curl -sS --max-time 5 --quote "DELE $RESULT_LOG_PATH" \
        "ftp://$PS5_HOST:2121/" >/dev/null 2>&1 || true
    curl -sS --fail --max-time 10 \
        "http://$PS5_HOST:8080/hbldr?pipe=0&daemon=1&path=$REMOTE_BASE/eboot.elf" \
        >/dev/null
    launch_status=$?
    if [ "$launch_status" -ne 0 ] && [ "$launch_status" -ne 28 ]; then
        exit 1
    fi
    result_ready=0
    attempts=0
    while [ "$attempts" -lt 30 ]; do
        if curl -sS --fail --max-time 3 "$result_ftp_url" \
                -o "$output_file" 2>/dev/null &&
           grep -q '^Graphics result:' "$output_file"; then
            result_ready=1
            break
        fi
        attempts=$((attempts + 1))
        sleep 1
    done
    if [ "$result_ready" -ne 1 ]; then
        echo "timed out waiting for file-backed graphics verdict" >&2
        exit 1
    fi
else
    curl -sS --fail --max-time 30 \
        "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=$REMOTE_BASE/eboot.elf" \
        > "$output_file" 2>&1
    transport_status=$?
fi
cat "$output_file"

grep -q "Runtime profile FW ABI $EXPECTED_FW_ABI: PASS" "$output_file" || exit 1
grep -q "GPU completion fence reached" "$output_file" || exit 1
grep -Fq "Render target $EXPECTED_TARGET at" "$output_file" || exit 1
if [ -n "$EXPECTED_VARIANT" ]; then
    grep -Fq "Graphics variant: $EXPECTED_VARIANT" "$output_file" || exit 1
fi
if [ -n "$EXPECTED_DRAW_MODE" ]; then
    grep -Fq "[Draw] $EXPECTED_DRAW_MODE: 0x00000000" \
        "$output_file" || exit 1
fi
if [ -n "$EXPECTED_PACKET_AUDIT" ]; then
    grep -Fq "$EXPECTED_PACKET_AUDIT" "$output_file" || exit 1
fi
if [ "$REQUIRE_MULTI_DRAW_ORACLE" -eq 1 ]; then
    grep -Fq '[Multi Draw] distinct second geometry: PASS' \
        "$output_file" || exit 1
fi
if [ "$REQUIRE_COUNT_BUFFER_ORACLE" -eq 1 ]; then
    grep -Fq '[Indirect Count] GPU-selected records=2: PASS' \
        "$output_file" || exit 1
fi
if [ "$REQUIRE_TESS_RINGS" -eq 1 ]; then
    grep -Fq '[Tess] reusable gfx1013 HS+TES+PS bind: 0x00000000' \
        "$output_file" || exit 1
    grep -Eq '^\[Tess Rings\] offchip changed=[1-9][0-9]* factor changed=4 invalid=0$' \
        "$output_file" || exit 1
    grep -Fq '[Tess Rings] mutation/value oracle: PASS' \
        "$output_file" || exit 1
fi
case "$EXPECTED_TARGET" in
    R16_UNORM)
        grep -Eq '^\[UNORM16\] GFX1013 R16_UNORM target: PASS$' \
            "$output_file" || exit 1
        grep -Eq '^\[UNORM16\] Stored components: 1; complete samples: [1-9][0-9]*; range=0x[0-9a-f]{4}\.\.0x[0-9a-f]{4}; out-of-range components: 0$' \
            "$output_file" || exit 1
        ;;
    RG16_UNORM)
        grep -Eq '^\[UNORM16\] GFX1013 RG16_UNORM target: PASS$' \
            "$output_file" || exit 1
        grep -Eq '^\[UNORM16\] Stored components: 2; complete samples: [1-9][0-9]*; range=0x[0-9a-f]{4}\.\.0x[0-9a-f]{4}; out-of-range components: 0$' \
            "$output_file" || exit 1
        grep -Eq '^\[UNORM16 Lane 0\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[UNORM16 Lane 1\].*: PASS$' "$output_file" || exit 1
        grep -q '^\[UNORM16\] Channel independence: PASS$' \
            "$output_file" || exit 1
        ;;
    RGBA16_UNORM)
        grep -Eq '^\[UNORM16\] GFX1013 RGBA16_UNORM target: PASS$' \
            "$output_file" || exit 1
        grep -Eq '^\[UNORM16\] Stored components: 4; complete samples: [1-9][0-9]*; range=0x[0-9a-f]{4}\.\.0x[0-9a-f]{4}; out-of-range components: 0$' \
            "$output_file" || exit 1
        grep -Eq '^\[UNORM16 Lane 0\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[UNORM16 Lane 1\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[UNORM16 Lane 2\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[UNORM16 Lane 3\].*: PASS$' "$output_file" || exit 1
        grep -q '^\[UNORM16\] Channel independence: PASS$' \
            "$output_file" || exit 1
        ;;
    R16_SNORM)
        grep -Eq '^\[SNORM16\] GFX1013 R16_SNORM target: PASS$' \
            "$output_file" || exit 1
        grep -Eq '^\[SNORM16\] Stored components: 1; complete samples: [1-9][0-9]*; signed-range=-[0-9]+\.\.[0-9]+; out-of-range components: 0$' \
            "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 0\].*: PASS$' "$output_file" || exit 1
        grep -q '^\[SNORM16\] Channel independence: PASS$' \
            "$output_file" || exit 1
        ;;
    RG16_SNORM)
        grep -Eq '^\[SNORM16\] GFX1013 RG16_SNORM target: PASS$' \
            "$output_file" || exit 1
        grep -Eq '^\[SNORM16\] Stored components: 2; complete samples: [1-9][0-9]*; signed-range=-[0-9]+\.\.[0-9]+; out-of-range components: 0$' \
            "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 0\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 1\].*: PASS$' "$output_file" || exit 1
        grep -q '^\[SNORM16\] Channel independence: PASS$' \
            "$output_file" || exit 1
        ;;
    RGBA16_SNORM)
        grep -Eq '^\[SNORM16\] GFX1013 RGBA16_SNORM target: PASS$' \
            "$output_file" || exit 1
        grep -Eq '^\[SNORM16\] Stored components: 4; complete samples: [1-9][0-9]*; signed-range=-[0-9]+\.\.[0-9]+; out-of-range components: 0$' \
            "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 0\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 1\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 2\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[SNORM16 Lane 3\].*: PASS$' "$output_file" || exit 1
        grep -q '^\[SNORM16\] Channel independence: PASS$' \
            "$output_file" || exit 1
        ;;
    R8_UNORM|RG8_UNORM)
        grep -Eq '^\[UNORM8\].*: PASS$' "$output_file" || exit 1
        ;;
    R32_FLOAT|RG32_FLOAT|RGBA32_FLOAT)
        grep -Eq '^\[FLOAT32\].*: PASS$' "$output_file" || exit 1
        ;;
    R32_UINT)
        grep -Eq '^\[UINT32 Lane 0\].*: PASS$' "$output_file" || exit 1
        grep -Eq '^\[UINT32\] changed=[1-9][0-9]*/[1-9][0-9]* .* independence=PASS: PASS$' \
            "$output_file" || exit 1
        ;;
    'offscreen RGB10A2')
        grep -Eq '^\[RGB10A2\] Packed top2 histogram:.*: PASS$' \
            "$output_file" || exit 1
        ;;
    'offscreen R11G11B10 FLOAT')
        grep -Eq '^\[R11G11B10\] Packed color FNV64:.*: PASS$' \
            "$output_file" || exit 1
        ;;
    RGBA8_UNORM|BGRA8_UNORM)
        grep -Eq '^\[RGBA8\] changed=[1-9][0-9]* distinct=[3-9][0-9]* packed-fnv64=0x[0-9a-f]{16}: PASS$' \
            "$output_file" || exit 1
        grep -q '^\[Vertex\] Interleaved buffer fetch: PASS$' \
            "$output_file" || exit 1
        grep -q '^\[Index\] Bound u16 indexed draw: PASS$' \
            "$output_file" || exit 1
        grep -q '^\[Texture\] gfx1013 image + bilinear sampler: PASS$' \
            "$output_file" || exit 1
        ;;
    RGBA8_SRGB|BGRA8_SRGB)
        grep -Eq '^\[sRGB\] transfer-mismatch=0 converted-channels=[1-9][0-9]*: PASS$' \
            "$output_file" || exit 1
        grep -q '^\[sRGB\] changed=[1-9][0-9]* coverage-mismatch=0 alpha-mismatch=0$' \
            "$output_file" || exit 1
        ;;
    *)
        grep -Fq "GFX1013 $EXPECTED_TARGET target: PASS" \
            "$output_file" || exit 1
        ;;
esac
grep -q "Driver shutdown: PASS" "$output_file" || exit 1
grep -q "Graphics result: PASS" "$output_file" || exit 1
if grep -Eq "FAIL|FATAL|MISMATCH|timed out" "$output_file"; then
    exit 1
fi
if [ "$transport_status" -ne 0 ]; then
    echo "graphics passed; websrv transport ended with curl=$transport_status" >&2
fi
