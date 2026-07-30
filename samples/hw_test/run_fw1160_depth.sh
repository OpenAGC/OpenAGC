#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
DEPTH_ARTIFACT=${DEPTH_ARTIFACT:?set DEPTH_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:-0x1160}
REMOTE_BASE=/data/homebrew/openagc_fw${EXPECTED_FW_ABI#0x}_depth

if [ ! -s "$DEPTH_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing depth-test or process-cleanup ELF" >&2
    exit 2
fi
if [ -n "${EXPECTED_ARTIFACT_SHA256:-}" ]; then
    actual_sha=$(shasum -a 256 "$DEPTH_ARTIFACT" | awk '{print $1}') || exit 2
    if [ "$actual_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
        echo "depth artifact SHA-256 mismatch" >&2
        echo "expected: $EXPECTED_ARTIFACT_SHA256" >&2
        echo "actual:   $actual_sha" >&2
        exit 2
    fi
fi

curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "ftp://$PS5_HOST:2121/data/homebrew/process_cleanup/eboot.elf" || exit 1
cleanup_launch_status=0
curl -sS --fail --max-time 10 \
    "http://$PS5_HOST:8080/hbldr?pipe=0&daemon=1&path=/data/homebrew/process_cleanup/eboot.elf" \
    >/dev/null || cleanup_launch_status=$?
if [ "$cleanup_launch_status" -ne 0 ] && \
   [ "$cleanup_launch_status" -ne 28 ]; then
    exit 1
fi
sleep 2
curl -sS --fail --max-time 5 "http://$PS5_HOST:8080/" >/dev/null || exit 1

curl -sS --fail --ftp-create-dirs -T "$DEPTH_ARTIFACT" \
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
        echo "timed out waiting for file-backed depth verdict" >&2
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
grep -Eq "\[Depth(\+Stencil)? Result\] markers=PASS color=PASS .*stencil=PASS" \
    "$output_file" || exit 1
if [ -n "${EXPECTED_HTILE_INITIAL:-}" ]; then
    grep -Eq "^\[HTILE Readback\] changed=[1-9][0-9]* other=[0-9]+ initial=$EXPECTED_HTILE_INITIAL$" \
        "$output_file" || exit 1
fi
if [ -n "${EXPECTED_HTILE_CHANGED:-}" ]; then
    grep -Eq "^\[HTILE Readback\] changed=$EXPECTED_HTILE_CHANGED other=[0-9]+ initial=$EXPECTED_HTILE_INITIAL$" \
        "$output_file" || exit 1
fi
if [ "${REQUIRE_HTILE_SUBRESOURCE:-0}" -eq 1 ]; then
    grep -Eq '^\[HTILE Subresource Readback\] selected-changed=[1-9][0-9]* outside-changed=0$' \
        "$output_file" || exit 1
fi
if [ -n "${EXPECTED_HTILE_SELECTED_CHANGED:-}" ]; then
    expected_htile_outside=${EXPECTED_HTILE_OUTSIDE_CHANGED:-0}
    case "$EXPECTED_HTILE_SELECTED_CHANGED:$expected_htile_outside" in
        *[!0-9:]*|:*|*:)
            echo "invalid HTILE subresource count" >&2
            exit 2
            ;;
    esac
    grep -Fqx "[HTILE Subresource Readback] selected-changed=$EXPECTED_HTILE_SELECTED_CHANGED outside-changed=$expected_htile_outside" \
        "$output_file" || exit 1
fi
if [ -n "${EXPECTED_COLOR_GREEN_RED:-}" ]; then
    case "$EXPECTED_COLOR_GREEN_RED" in
        ''|*[!0-9]*)
            echo "invalid EXPECTED_COLOR_GREEN_RED" >&2
            exit 2
            ;;
    esac
    grep -Fq "[Depth Readback] green=$EXPECTED_COLOR_GREEN_RED red=$EXPECTED_COLOR_GREEN_RED " \
        "$output_file" || exit 1
fi
if [ "${REQUIRE_MSAA_RESOLVE:-0}" -eq 1 ]; then
    grep -Eq '^\[MSAA\] shader-resolved 4x RGBA8 to 1x (headless|VideoOut) target$' \
        "$output_file" || exit 1
    grep -Eq '^\[Depth Readback\] green=[1-9][0-9]* red=[1-9][0-9]* ' \
        "$output_file" || exit 1
    grep -Eq '^\[Depth Readback\] raw D32: one=[1-9][0-9]* near=[1-9][0-9]* far=[1-9][0-9]*$' \
        "$output_file" || exit 1
    grep -q '^\[Depth+4xMSAA Result\] markers=PASS color=PASS raw-depth=PASS stencil=PASS$' \
        "$output_file" || exit 1
fi
if [ "${EXPECTED_D16_FULL_RECT:-0}" -eq 1 ]; then
    grep -q '^\[Depth Readback\] green=228096 red=228096 ' \
        "$output_file" || exit 1
    grep -q '^\[Depth Readback\] raw D16: one=1617408 near=228096 far=228096$' \
        "$output_file" || exit 1
fi
if [ "${EXPECTED_D32_FULL_RECT:-0}" -eq 1 ]; then
    grep -q '^\[Depth Readback\] green=228096 red=228096 ' \
        "$output_file" || exit 1
fi
if [ "${EXPECTED_D32_FULL_RECT:-0}" -eq 1 ] ||
   [ -n "${EXPECTED_D32_ONE_COUNT:-}" ] ||
   [ -n "${EXPECTED_D32_NEAR_COUNT:-}" ] ||
   [ -n "${EXPECTED_D32_FAR_COUNT:-}" ]; then
    expected_d32_one=${EXPECTED_D32_ONE_COUNT:-1617408}
    expected_d32_near=${EXPECTED_D32_NEAR_COUNT:-228096}
    expected_d32_far=${EXPECTED_D32_FAR_COUNT:-228096}
    case "$expected_d32_one:$expected_d32_near:$expected_d32_far" in
        *[!0-9:]*|:*|*:|*::* )
            echo "invalid expected D32 count" >&2
            exit 2
            ;;
    esac
    grep -Fqx "[Depth Readback] raw D32: one=$expected_d32_one near=$expected_d32_near far=$expected_d32_far" \
        "$output_file" || exit 1
fi
if [ "${EXPECTED_STENCIL_FULL_RECT:-0}" -eq 1 ]; then
    grep -q '^\[Stencil Readback\] zero=2165248 replace-5a=456192 other=0$' \
        "$output_file" || exit 1
fi
if [ -n "${EXPECTED_COMBINED_EXPCLEAR_ASPECTS:-}" ]; then
    grep -Eq "^\[Combined Expclear RMW\] aspects=0x$EXPECTED_COMBINED_EXPCLEAR_ASPECTS gate=ON .* mismatch=0 outside-changed=0 reserved=PASS fence=48544c45: PASS$" \
        "$output_file" || exit 1
fi
grep -q "Driver shutdown: PASS" "$output_file" || exit 1
grep -q "Graphics result: PASS" "$output_file" || exit 1
if grep -Eq "FAIL|FATAL|MISMATCH|timed out" "$output_file"; then
    exit 1
fi
if [ "$transport_status" -ne 0 ]; then
    echo "depth gate passed; websrv transport ended with curl=$transport_status" >&2
fi
