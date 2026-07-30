#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run one pinned firmware-neutral ELF with bounded file-backed verdicts.

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
PORTABILITY_ARTIFACT=${PORTABILITY_ARTIFACT:?set PORTABILITY_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
FIRMWARE_PROBE_ELF=${FIRMWARE_PROBE_ELF:?set FIRMWARE_PROBE_ELF}
EXPECTED_ARTIFACT_SHA256=${EXPECTED_ARTIFACT_SHA256:?set EXPECTED_ARTIFACT_SHA256}
EXPECTED_CLEANUP_SHA256=${EXPECTED_CLEANUP_SHA256:?set EXPECTED_CLEANUP_SHA256}
EXPECTED_PROBE_SHA256=${EXPECTED_PROBE_SHA256:?set EXPECTED_PROBE_SHA256}
EXPECTED_FIRMWARE_ABI_KEY=${EXPECTED_FIRMWARE_ABI_KEY:?set EXPECTED_FIRMWARE_ABI_KEY}
PORTABILITY_ITERATIONS=${PORTABILITY_ITERATIONS:-2}

case "$PORTABILITY_ITERATIONS" in
    ''|*[!0-9]*) echo "PORTABILITY_ITERATIONS must be a positive integer" >&2; exit 2 ;;
esac
if [ "$PORTABILITY_ITERATIONS" -lt 1 ]; then
    echo "PORTABILITY_ITERATIONS must be at least one" >&2
    exit 2
fi
case "$EXPECTED_FIRMWARE_ABI_KEY" in
    [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) ;;
    *) echo "EXPECTED_FIRMWARE_ABI_KEY must contain four hex digits" >&2; exit 2 ;;
esac
EXPECTED_FIRMWARE_ABI_KEY=$(printf '%s' "$EXPECTED_FIRMWARE_ABI_KEY" | tr 'A-F' 'a-f')

check_local_sha()
{
    label=$1
    path=$2
    expected=$3

    if [ ! -s "$path" ]; then
        echo "missing $label: $path" >&2
        exit 2
    fi
    actual=$(shasum -a 256 "$path" | awk '{print $1}') || exit 2
    if [ "$actual" != "$expected" ]; then
        echo "$label SHA-256 mismatch" >&2
        echo "expected: $expected" >&2
        echo "actual:   $actual" >&2
        exit 2
    fi
}

check_local_sha "portability artifact" "$PORTABILITY_ARTIFACT" \
    "$EXPECTED_ARTIFACT_SHA256"
check_local_sha "cleanup artifact" "$PROCESS_CLEANUP_ELF" \
    "$EXPECTED_CLEANUP_SHA256"
check_local_sha "firmware probe" "$FIRMWARE_PROBE_ELF" \
    "$EXPECTED_PROBE_SHA256"

remote_base=/data/homebrew/openagc_portability
remote_path=$remote_base/eboot.elf
cleanup_path=/data/homebrew/openagc_process_cleanup/eboot.elf
probe_path=$remote_base/firmware_probe.elf
result_path=$remote_base/result.log
preflight_path=$remote_base/preflight.log
ftp_root=ftp://$PS5_HOST:2121
http_root=http://$PS5_HOST:8080
output_file=$(mktemp) || exit 2
uploaded_file=$(mktemp) || exit 2
preflight_file=$(mktemp) || exit 2
trap 'rm -f "$output_file" "$uploaded_file" "$preflight_file"' EXIT HUP INT TERM

upload_and_verify()
{
    label=$1
    local_path=$2
    remote=$3
    expected=$4

    curl -sS --fail --ftp-create-dirs -T "$local_path" \
        "$ftp_root$remote" || exit 1
    curl -sS --fail "$ftp_root$remote" -o "$uploaded_file" || exit 1
    uploaded_sha=$(shasum -a 256 "$uploaded_file" | awk '{print $1}') || exit 1
    if [ "$uploaded_sha" != "$expected" ]; then
        echo "uploaded $label SHA-256 mismatch" >&2
        exit 1
    fi
}

curl -sS --fail --max-time 5 "$http_root/" >/dev/null || exit 1
upload_and_verify "cleanup artifact" "$PROCESS_CLEANUP_ELF" \
    "$cleanup_path" "$EXPECTED_CLEANUP_SHA256"
upload_and_verify "firmware probe" "$FIRMWARE_PROBE_ELF" \
    "$probe_path" "$EXPECTED_PROBE_SHA256"

# A stale renderer is killed before even the read-only firmware probe. The
# portability payload is not uploaded or launched until the exact console key
# has been observed in this fresh file-backed verdict.
curl -sS --max-time 5 --quote "DELE $preflight_path" \
    "$ftp_root/" >/dev/null 2>&1 || true
curl -sS --fail --max-time 10 \
    "$http_root/hbldr?pipe=0&daemon=1&path=$cleanup_path" \
    >/dev/null || exit 1
sleep 1
curl -sS --max-time 10 \
    "$http_root/hbldr?pipe=0&daemon=1&path=$probe_path" \
    >/dev/null
probe_status=$?
if [ "$probe_status" -ne 0 ] && [ "$probe_status" -ne 28 ]; then
    exit 1
fi

preflight_ready=0
attempts=0
while [ "$attempts" -lt 15 ]; do
    if curl -sS --fail --max-time 3 "$ftp_root$preflight_path" \
            -o "$preflight_file" 2>/dev/null &&
       grep -q '^Firmware preflight result:' "$preflight_file"; then
        preflight_ready=1
        break
    fi
    attempts=$((attempts + 1))
    sleep 1
done
if [ "$preflight_ready" -ne 1 ]; then
    echo "timed out waiting for firmware preflight verdict" >&2
    exit 1
fi
if ! grep -Eq "^Firmware preflight raw=0x${EXPECTED_FIRMWARE_ABI_KEY}[0-9a-f]{4} key=0x${EXPECTED_FIRMWARE_ABI_KEY} " \
        "$preflight_file" ||
   ! grep -q '^Firmware preflight result: PASS$' "$preflight_file" ||
   grep -Eq 'FAIL|FATAL|MISMATCH|kernel panic' "$preflight_file"; then
    echo "firmware preflight rejected the console" >&2
    cat "$preflight_file"
    exit 1
fi
grep -E '^Firmware preflight (raw|result)' "$preflight_file"

upload_and_verify "portability artifact" "$PORTABILITY_ARTIFACT" \
    "$remote_path" "$EXPECTED_ARTIFACT_SHA256"
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
