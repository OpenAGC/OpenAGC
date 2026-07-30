#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
COPY_ARTIFACT=${COPY_ARTIFACT:?set COPY_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:-0x1160}
EXPECTED_COPY_HASH=${EXPECTED_COPY_HASH:-0xdd3702089b80f950}
RESULT_LOG_PATH=${RESULT_LOG_PATH:-}
REMOTE_BASE=/data/homebrew/openagc_fw${EXPECTED_FW_ABI#0x}_copy

if [ ! -s "$COPY_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing copy-test or process-cleanup ELF" >&2
    exit 2
fi

curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "ftp://$PS5_HOST:2121/data/homebrew/process_cleanup/eboot.elf" || exit 1
curl -sS --fail --max-time 10 \
    "http://$PS5_HOST:8080/hbldr?pipe=0&daemon=1&path=/data/homebrew/process_cleanup/eboot.elf" \
    >/dev/null || exit 1
sleep 2
curl -sS --fail --max-time 5 "http://$PS5_HOST:8080/" >/dev/null || exit 1

curl -sS --fail --ftp-create-dirs -T "$COPY_ARTIFACT" \
    "ftp://$PS5_HOST:2121$REMOTE_BASE/eboot.elf" || exit 1
output_file=$(mktemp) || exit 2
trap 'rm -f "$output_file"' EXIT HUP INT TERM

if [ -n "$RESULT_LOG_PATH" ]; then
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
           grep -q '^Copy result:' "$output_file"; then
            result_ready=1
            break
        fi
        attempts=$((attempts + 1))
        sleep 1
    done
    if [ "$result_ready" -ne 1 ]; then
        echo "timed out waiting for file-backed copy verdict" >&2
        exit 1
    fi
else
    curl -sS --fail --max-time 30 \
        "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=$REMOTE_BASE/eboot.elf" \
        > "$output_file" 2>&1 || exit 1
fi
cat "$output_file"

grep -q "Runtime profile FW ABI $EXPECTED_FW_ABI: PASS" "$output_file" || exit 1
grep -Eq '^Copy submit: 0x00000000 dwords=[1-9][0-9]* dma-packets=4 bytes=8294400$' \
    "$output_file" || exit 1
grep -q '^Copy completion fence: PASS (0x00000000)$' "$output_file" || exit 1
grep -Fq "Copy compare: mismatches=0 first=4294967295 source-fnv64=$EXPECTED_COPY_HASH destination-fnv64=$EXPECTED_COPY_HASH: PASS" \
    "$output_file" || exit 1
grep -q '^Driver shutdown: PASS (0x00000000)$' "$output_file" || exit 1
grep -q '^Copy result: PASS$' "$output_file" || exit 1
if grep -Eq 'FAIL|FATAL|MISMATCH|timed out' "$output_file"; then
    exit 1
fi
