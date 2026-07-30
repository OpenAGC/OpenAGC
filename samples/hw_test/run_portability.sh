#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run one pinned firmware-neutral ELF with bounded file-backed verdicts.

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
PORTABILITY_ARTIFACT=${PORTABILITY_ARTIFACT:?set PORTABILITY_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_ARTIFACT_SHA256=${EXPECTED_ARTIFACT_SHA256:?set EXPECTED_ARTIFACT_SHA256}
EXPECTED_FIRMWARE_ABI_KEY=${EXPECTED_FIRMWARE_ABI_KEY:?set EXPECTED_FIRMWARE_ABI_KEY}
PORTABILITY_ITERATIONS=${PORTABILITY_ITERATIONS:-2}

case "$PORTABILITY_ITERATIONS" in
    ''|*[!0-9]*) echo "PORTABILITY_ITERATIONS must be a positive integer" >&2; exit 2 ;;
esac
if [ "$PORTABILITY_ITERATIONS" -lt 1 ]; then
    echo "PORTABILITY_ITERATIONS must be at least one" >&2
    exit 2
fi
if [ ! -s "$PORTABILITY_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing portability or cleanup artifact" >&2
    exit 2
fi

actual_sha=$(shasum -a 256 "$PORTABILITY_ARTIFACT" | awk '{print $1}') || exit 2
if [ "$actual_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
    echo "portability artifact SHA-256 mismatch" >&2
    echo "expected: $EXPECTED_ARTIFACT_SHA256" >&2
    echo "actual:   $actual_sha" >&2
    exit 2
fi

remote_base=/data/homebrew/openagc_portability
remote_path=$remote_base/eboot.elf
cleanup_path=/data/homebrew/openagc_process_cleanup/eboot.elf
result_path=$remote_base/result.log
ftp_root=ftp://$PS5_HOST:2121
http_root=http://$PS5_HOST:8080
output_file=$(mktemp) || exit 2
uploaded_file=$(mktemp) || exit 2
trap 'rm -f "$output_file" "$uploaded_file"' EXIT HUP INT TERM

curl -sS --fail --max-time 5 "$http_root/" >/dev/null || exit 1
curl -sS --fail --ftp-create-dirs -T "$PORTABILITY_ARTIFACT" \
    "$ftp_root$remote_path" || exit 1
curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "$ftp_root$cleanup_path" || exit 1
curl -sS --fail "$ftp_root$remote_path" -o "$uploaded_file" || exit 1
uploaded_sha=$(shasum -a 256 "$uploaded_file" | awk '{print $1}') || exit 1
if [ "$uploaded_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
    echo "uploaded portability artifact SHA-256 mismatch" >&2
    exit 1
fi
echo "Pinned portability ELF: $EXPECTED_ARTIFACT_SHA256"

iteration=1
while [ "$iteration" -le "$PORTABILITY_ITERATIONS" ]; do
    curl -sS --max-time 5 --quote "DELE $result_path" \
        "$ftp_root/" >/dev/null 2>&1 || true
    : > "$output_file"

    echo "[$iteration/$PORTABILITY_ITERATIONS] cleanup immediately before payload"
    curl -sS --fail --max-time 10 \
        "$http_root/hbldr?pipe=0&daemon=1&path=$cleanup_path" \
        >/dev/null || exit 1
    sleep 1

    curl -sS --max-time 10 \
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
           grep -q '^Portability result:' "$output_file"; then
            ready=1
            break
        fi
        attempts=$((attempts + 1))
        sleep 1
    done
    if [ "$ready" -ne 1 ]; then
        echo "timed out waiting for portability verdict" >&2
        exit 1
    fi

    grep -Eq "^Runtime profile raw=0x[0-9a-f]{8} key=0x${EXPECTED_FIRMWARE_ABI_KEY} .*: PASS$" \
        "$output_file" || exit 1
    grep -q '^VideoOut driver defaults/async: PASS/PASS$' "$output_file" || exit 1
    grep -q '^Public VideoOut open/register/restore: 0x00000000 PASS$' \
        "$output_file" || exit 1
    grep -q '^VideoOut GPU marker: value=0x504f5254 .* PASS$' \
        "$output_file" || exit 1
    grep -q '^Public VideoOut bounded flips: PASS$' "$output_file" || exit 1
    grep -q '^Public VideoOut cleanup: shutdown=0x00000000 submit=0x00000000 unmap=0x00000000 direct=0x00000000$' \
        "$output_file" || exit 1
    grep -q '^Portability result: PASS$' "$output_file" || exit 1
    if grep -Eq 'FAIL|FATAL|MISMATCH|timed out|kernel panic' "$output_file"; then
        cat "$output_file"
        exit 1
    fi

    grep -E 'Runtime profile|defaults/async|open/register/restore|GPU marker|bounded flips|VideoOut cleanup|Portability result' \
        "$output_file"
    iteration=$((iteration + 1))
done

echo "Portability FW 0x${EXPECTED_FIRMWARE_ABI_KEY}: ${PORTABILITY_ITERATIONS}/${PORTABILITY_ITERATIONS} PASS"
