#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Repeated-launch gate for sample-owned flexible-memory teardown.

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
STRESS_ARTIFACT=${STRESS_ARTIFACT:?set STRESS_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:?set EXPECTED_FW_ABI}
EXPECTED_ARTIFACT_SHA256=${EXPECTED_ARTIFACT_SHA256:?set EXPECTED_ARTIFACT_SHA256}
RESULT_LOG_PATH=${RESULT_LOG_PATH:?set RESULT_LOG_PATH}
STRESS_ITERATIONS=${STRESS_ITERATIONS:-14}

case "$EXPECTED_FW_ABI" in
    0x0550|0x1160) ;;
    *) echo "unsupported stress firmware ABI: $EXPECTED_FW_ABI" >&2; exit 2 ;;
esac
case "$STRESS_ITERATIONS" in
    ''|*[!0-9]*) echo "STRESS_ITERATIONS must be a positive integer" >&2; exit 2 ;;
esac
if [ "$STRESS_ITERATIONS" -lt 1 ]; then
    echo "STRESS_ITERATIONS must be at least one" >&2
    exit 2
fi
if [ ! -s "$STRESS_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing stress or cleanup artifact" >&2
    exit 2
fi

actual_sha=$(shasum -a 256 "$STRESS_ARTIFACT" | awk '{print $1}') || exit 2
if [ "$actual_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
    echo "stress artifact SHA-256 mismatch" >&2
    echo "expected: $EXPECTED_ARTIFACT_SHA256" >&2
    echo "actual:   $actual_sha" >&2
    exit 2
fi

fw_suffix=${EXPECTED_FW_ABI#0x}
remote_base=/data/homebrew/openagc_fw${fw_suffix}_cleanup_stress
expected_result_path=$remote_base/result.log
if [ "$RESULT_LOG_PATH" != "$expected_result_path" ]; then
    echo "cleanup-stress result path mismatch" >&2
    echo "expected: $expected_result_path" >&2
    echo "actual:   $RESULT_LOG_PATH" >&2
    exit 2
fi
cleanup_path=/data/homebrew/openagc_process_cleanup/eboot.elf
ftp_root=ftp://$PS5_HOST:2121
http_root=http://$PS5_HOST:8080
result_url=$ftp_root$RESULT_LOG_PATH
output_file=$(mktemp) || exit 2
trap 'rm -f "$output_file"' EXIT HUP INT TERM

curl -sS --fail --ftp-create-dirs -T "$STRESS_ARTIFACT" \
    "$ftp_root$remote_base/eboot.elf" || exit 1
curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "$ftp_root$cleanup_path" || exit 1

iteration=1
while [ "$iteration" -le "$STRESS_ITERATIONS" ]; do
    curl -sS --max-time 5 --quote "DELE $RESULT_LOG_PATH" \
        "$ftp_root/" >/dev/null 2>&1 || true
    : > "$output_file"

    echo "[$iteration/$STRESS_ITERATIONS] cleanup"
    curl -sS --fail --max-time 10 \
        "$http_root/hbldr?pipe=0&daemon=1&path=$cleanup_path" \
        >/dev/null || exit 1
    sleep 2
    curl -sS --fail --max-time 5 "$http_root/" >/dev/null || exit 1

    echo "[$iteration/$STRESS_ITERATIONS] graphics"
    curl -sS --fail --max-time 10 \
        "$http_root/hbldr?pipe=0&daemon=1&path=$remote_base/eboot.elf" \
        >/dev/null
    launch_status=$?
    if [ "$launch_status" -ne 0 ] && [ "$launch_status" -ne 28 ]; then
        exit 1
    fi

    ready=0
    attempts=0
    while [ "$attempts" -lt 30 ]; do
        if curl -sS --fail --max-time 3 "$result_url" \
                -o "$output_file" 2>/dev/null &&
           grep -q '^Graphics result:' "$output_file"; then
            ready=1
            break
        fi
        attempts=$((attempts + 1))
        sleep 1
    done
    if [ "$ready" -ne 1 ]; then
        echo "timed out waiting for cleanup-stress verdict" >&2
        exit 1
    fi

    grep -q "Runtime profile FW ABI $EXPECTED_FW_ABI: PASS" \
        "$output_file" || exit 1
    grep -q 'GPU completion fence reached' "$output_file" || exit 1
    grep -q '^Graphics memory cleanup: pool=0x00000000 cb=0x00000000 unmap=0x00000000 direct=0x00000000$' \
        "$output_file" || exit 1
    grep -q '^Driver shutdown: PASS' "$output_file" || exit 1
    grep -q '^Graphics result: PASS$' "$output_file" || exit 1
    if grep -Eq 'FAIL|FATAL|MISMATCH|timed out|kernel panic' "$output_file"; then
        cat "$output_file"
        exit 1
    fi

    grep -E 'Runtime profile|GPU completion|Graphics memory cleanup|Driver shutdown|Graphics result' \
        "$output_file"
    iteration=$((iteration + 1))
done

echo "Flexible cleanup stress: $STRESS_ITERATIONS/$STRESS_ITERATIONS PASS"
