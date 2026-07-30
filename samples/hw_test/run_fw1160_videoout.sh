#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Bounded file-backed FW 11.60 public VideoOut integration gate.

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
VIDEOOUT_ARTIFACT=${VIDEOOUT_ARTIFACT:?set VIDEOOUT_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_ARTIFACT_SHA256=${EXPECTED_ARTIFACT_SHA256:?set EXPECTED_ARTIFACT_SHA256}
VIDEOOUT_ITERATIONS=${VIDEOOUT_ITERATIONS:-2}

case "$VIDEOOUT_ITERATIONS" in
    ''|*[!0-9]*) echo "VIDEOOUT_ITERATIONS must be a positive integer" >&2; exit 2 ;;
esac
if [ "$VIDEOOUT_ITERATIONS" -lt 1 ]; then
    echo "VIDEOOUT_ITERATIONS must be at least one" >&2
    exit 2
fi
if [ ! -s "$VIDEOOUT_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing VideoOut or cleanup artifact" >&2
    exit 2
fi

actual_sha=$(shasum -a 256 "$VIDEOOUT_ARTIFACT" | awk '{print $1}') || exit 2
if [ "$actual_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
    echo "VideoOut artifact SHA-256 mismatch" >&2
    echo "expected: $EXPECTED_ARTIFACT_SHA256" >&2
    echo "actual:   $actual_sha" >&2
    exit 2
fi

remote_base=/data/homebrew/openagc_fw1160_videoout
remote_path=$remote_base/eboot.elf
cleanup_path=/data/homebrew/openagc_process_cleanup/eboot.elf
result_path=$remote_base/result.log
ftp_root=ftp://$PS5_HOST:2121
http_root=http://$PS5_HOST:8080
output_file=$(mktemp) || exit 2
trap 'rm -f "$output_file"' EXIT HUP INT TERM

curl -sS --fail --ftp-create-dirs -T "$VIDEOOUT_ARTIFACT" \
    "$ftp_root$remote_path" || exit 1
curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "$ftp_root$cleanup_path" || exit 1

iteration=1
while [ "$iteration" -le "$VIDEOOUT_ITERATIONS" ]; do
    curl -sS --max-time 5 --quote "DELE $result_path" \
        "$ftp_root/" >/dev/null 2>&1 || true
    : > "$output_file"

    echo "[$iteration/$VIDEOOUT_ITERATIONS] cleanup"
    curl -sS --fail --max-time 10 \
        "$http_root/hbldr?pipe=0&daemon=1&path=$cleanup_path" \
        >/dev/null || exit 1
    sleep 2
    curl -sS --fail --max-time 5 "$http_root/" >/dev/null || exit 1

    echo "[$iteration/$VIDEOOUT_ITERATIONS] public VideoOut"
    curl -sS --fail --max-time 10 \
        "$http_root/hbldr?pipe=0&daemon=1&path=$remote_path" \
        >/dev/null
    launch_status=$?
    if [ "$launch_status" -ne 0 ] && [ "$launch_status" -ne 28 ]; then
        exit 1
    fi

    ready=0
    attempts=0
    while [ "$attempts" -lt 30 ]; do
        if curl -sS --fail --max-time 3 "$ftp_root$result_path" \
                -o "$output_file" 2>/dev/null &&
           grep -q '^Public VideoOut result:' "$output_file"; then
            ready=1
            break
        fi
        attempts=$((attempts + 1))
        sleep 1
    done
    if [ "$ready" -ne 1 ]; then
        echo "timed out waiting for VideoOut verdict" >&2
        exit 1
    fi

    grep -q '^Runtime profile FW ABI 0x1160: PASS$' "$output_file" || exit 1
    grep -q '^VideoOut driver defaults/async: PASS/PASS$' "$output_file" || exit 1
    grep -q '^Public VideoOut open/register/restore: 0x00000000 PASS$' \
        "$output_file" || exit 1
    grep -q '^VideoOut GPU marker: .* PASS$' "$output_file" || exit 1
    grep -q '^Public VideoOut bounded flips: PASS$' "$output_file" || exit 1
    grep -q '^Public VideoOut cleanup: shutdown=0x00000000 submit=0x00000000 unmap=0x00000000 direct=0x00000000$' \
        "$output_file" || exit 1
    grep -q '^Public VideoOut result: PASS$' "$output_file" || exit 1
    if grep -Eq 'FAIL|FATAL|MISMATCH|timed out|kernel panic' "$output_file"; then
        cat "$output_file"
        exit 1
    fi

    grep -E 'Runtime profile|defaults/async|open/register/restore|GPU marker|bounded flips|VideoOut cleanup|VideoOut result' \
        "$output_file"
    iteration=$((iteration + 1))
done

echo "FW 11.60 public VideoOut: $VIDEOOUT_ITERATIONS/$VIDEOOUT_ITERATIONS PASS"
