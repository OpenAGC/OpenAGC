#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Hash-pinned cleanup-first runner for native-runtime hardware gates.

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
RUNTIME_ARTIFACT=${RUNTIME_ARTIFACT:?set RUNTIME_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:?set EXPECTED_FW_ABI}
EXPECTED_ARTIFACT_SHA256=${EXPECTED_ARTIFACT_SHA256:?set EXPECTED_ARTIFACT_SHA256}
EXPECTED_VERDICT=${EXPECTED_VERDICT:?set EXPECTED_VERDICT}
REMOTE_NAME=${REMOTE_NAME:?set REMOTE_NAME}
RUN_TIMEOUT_SECONDS=${RUN_TIMEOUT_SECONDS:-30}

case "$EXPECTED_FW_ABI" in
    0x0550|0x1160) ;;
    *) echo "unsupported runtime-gate firmware ABI: $EXPECTED_FW_ABI" >&2; exit 2 ;;
esac
case "$REMOTE_NAME" in
    ''|*[!A-Za-z0-9_-]*) echo "invalid runtime-gate remote name" >&2; exit 2 ;;
esac
case "$RUN_TIMEOUT_SECONDS" in
    ''|*[!0-9]*) echo "runtime-gate timeout must be a positive integer" >&2; exit 2 ;;
esac
if [ "$RUN_TIMEOUT_SECONDS" -lt 1 ]; then
    echo "runtime-gate timeout must be at least one second" >&2
    exit 2
fi
if [ ! -s "$RUNTIME_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing runtime-gate or process-cleanup ELF" >&2
    exit 2
fi

actual_sha=$(shasum -a 256 "$RUNTIME_ARTIFACT" | awk '{print $1}') || exit 2
if [ "$actual_sha" != "$EXPECTED_ARTIFACT_SHA256" ]; then
    echo "runtime-gate artifact SHA-256 mismatch" >&2
    echo "expected: $EXPECTED_ARTIFACT_SHA256" >&2
    echo "actual:   $actual_sha" >&2
    exit 2
fi

ftp_root=ftp://$PS5_HOST:2121
http_root=http://$PS5_HOST:8080
cleanup_path=/data/homebrew/openagc_process_cleanup/eboot.elf
remote_base=/data/homebrew/$REMOTE_NAME
output_file=$(mktemp) || exit 2
trap 'rm -f "$output_file"' EXIT HUP INT TERM

curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "$ftp_root$cleanup_path" || exit 1
curl -sS --fail --ftp-create-dirs -T "$RUNTIME_ARTIFACT" \
    "$ftp_root$remote_base/eboot.elf" || exit 1

cleanup_status=0
curl -sS --fail --max-time 10 \
    "$http_root/hbldr?pipe=0&daemon=1&path=$cleanup_path" \
    >/dev/null || cleanup_status=$?
if [ "$cleanup_status" -ne 0 ] && [ "$cleanup_status" -ne 28 ]; then
    echo "process-cleanup launch failed: curl=$cleanup_status" >&2
    exit 1
fi
sleep 2
curl -sS --fail --max-time 5 "$http_root/" >/dev/null || exit 1

transport_status=0
curl -sS --fail --max-time "$RUN_TIMEOUT_SECONDS" \
    "$http_root/hbldr?pipe=1&daemon=0&path=$remote_base/eboot.elf" \
    > "$output_file" 2>&1 || transport_status=$?
cat "$output_file"
if [ "$transport_status" -ne 0 ]; then
    echo "runtime-gate transport failed: curl=$transport_status" >&2
    exit 1
fi

grep -Eq "^Runtime profile: .+ \(FW ABI $EXPECTED_FW_ABI\)$" \
    "$output_file" || exit 1
grep -Fqx "$EXPECTED_VERDICT" "$output_file" || exit 1
grep -Fqx 'agcDestroyDevice: 0x00000000 (AGC_OK)' "$output_file" || exit 1
if grep -Eq 'AGC_ERROR_| FAIL$|FATAL|MISMATCH|timed out|kernel panic' \
        "$output_file"; then
    exit 1
fi
curl -sS --fail --max-time 5 "$http_root/" >/dev/null || exit 1

echo "Guarded runtime gate: $EXPECTED_VERDICT"
