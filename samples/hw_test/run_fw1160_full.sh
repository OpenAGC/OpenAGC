#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
FULL_ARTIFACT=${FULL_ARTIFACT:?set FULL_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
REMOTE_BASE=/data/homebrew/openagc_fw1160_full

if [ ! -s "$FULL_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing full-test or process-cleanup ELF" >&2
    exit 2
fi

# The cleanup payload must be the immediately preceding homebrew launch.
curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "ftp://$PS5_HOST:2121/data/homebrew/process_cleanup/eboot.elf" || exit 1
curl -sS --max-time 20 \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/process_cleanup/eboot.elf" || exit 1
sleep 2

curl -sS --fail --ftp-create-dirs -T "$FULL_ARTIFACT" \
    "ftp://$PS5_HOST:2121$REMOTE_BASE/eboot.elf" || exit 1
output_file=$(mktemp) || exit 2
trap 'rm -f "$output_file"' EXIT HUP INT TERM
curl -sS --fail --max-time 30 \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=$REMOTE_BASE/eboot.elf" \
    > "$output_file" 2>&1
transport_status=$?
cat "$output_file"

grep -q "Runtime profile FW ABI 0x1160: PASS" "$output_file" || exit 1
grep -q "Driver shutdown:   PASS" "$output_file" || exit 1
grep -q "Probe result: PASS" "$output_file" || exit 1
if grep -Eq "FAIL|FATAL" "$output_file"; then
    exit 1
fi
if [ "$transport_status" -ne 0 ]; then
    echo "probe passed; websrv transport ended with curl=$transport_status" >&2
fi
