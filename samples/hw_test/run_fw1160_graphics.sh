#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
GRAPHICS_ARTIFACT=${GRAPHICS_ARTIFACT:?set GRAPHICS_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_TARGET=${EXPECTED_TARGET:-offscreen FP16}
EXPECTED_DRAW_MODE=${EXPECTED_DRAW_MODE:-}
EXPECTED_VARIANT=${EXPECTED_VARIANT:-}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:-0x1160}
REMOTE_BASE=/data/homebrew/openagc_fw${EXPECTED_FW_ABI#0x}_graphics

if [ ! -s "$GRAPHICS_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing graphics-test or process-cleanup ELF" >&2
    exit 2
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
trap 'rm -f "$output_file"' EXIT HUP INT TERM
transport_status=0
if [ -n "${RESULT_LOG_PATH:-}" ]; then
    result_ftp_url="ftp://$PS5_HOST:2121$RESULT_LOG_PATH"
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
case "$EXPECTED_TARGET" in
    R8_UNORM|RG8_UNORM)
        grep -Eq '^\[UNORM8\].*: PASS$' "$output_file" || exit 1
        ;;
    R32_FLOAT|RG32_FLOAT|RGBA32_FLOAT)
        grep -Eq '^\[FLOAT32\].*: PASS$' "$output_file" || exit 1
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
