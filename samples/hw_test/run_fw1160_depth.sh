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
curl -sS --fail --max-time 10 \
    "http://$PS5_HOST:8080/hbldr?pipe=0&daemon=1&path=/data/homebrew/process_cleanup/eboot.elf" \
    >/dev/null || exit 1
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
if [ "${EXPECTED_D16_FULL_RECT:-0}" -eq 1 ]; then
    grep -q '^\[Depth Readback\] green=228096 red=228096 ' \
        "$output_file" || exit 1
    grep -q '^\[Depth Readback\] raw D16: one=1617408 near=228096 far=228096$' \
        "$output_file" || exit 1
fi
if [ "${EXPECTED_D32_FULL_RECT:-0}" -eq 1 ]; then
    grep -q '^\[Depth Readback\] green=228096 red=228096 ' \
        "$output_file" || exit 1
    grep -q '^\[Depth Readback\] raw D32: one=1617408 near=228096 far=228096$' \
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
