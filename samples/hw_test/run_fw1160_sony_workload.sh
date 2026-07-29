#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
ORACLE_ARTIFACT=${ORACLE_ARTIFACT:?set ORACLE_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
ALLOW_SONY_DRIVER_ORACLE=${ALLOW_SONY_DRIVER_ORACLE:-NO}
REMOTE_BASE=/data/homebrew/openagc_fw1160_sony_workload

if [ "$ALLOW_SONY_DRIVER_ORACLE" != YES ]; then
    echo "refusing installed-driver oracle without ALLOW_SONY_DRIVER_ORACLE=YES" >&2
    exit 2
fi
if [ ! -s "$ORACLE_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing oracle or process-cleanup ELF" >&2
    exit 2
fi

# The cleanup payload must be the immediately preceding homebrew launch.
curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "ftp://$PS5_HOST:2121/data/homebrew/process_cleanup/eboot.elf" || exit 1
curl -sS --max-time 20 \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/process_cleanup/eboot.elf" || exit 1
sleep 2

curl -sS --fail --ftp-create-dirs -T "$ORACLE_ARTIFACT" \
    "ftp://$PS5_HOST:2121$REMOTE_BASE/eboot.elf" || exit 1
output_file=$(mktemp) || exit 2
trap 'rm -f "$output_file"' EXIT HUP INT TERM
curl -sS --fail --max-time 25 \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=$REMOTE_BASE/eboot.elf" \
    > "$output_file" 2>&1
transport_status=$?
cat "$output_file"

if ! grep -q "FW 11.60 installed-driver workload oracle:" "$output_file"; then
    echo "oracle produced no verdict; launching cleanup payload" >&2
    curl -sS --max-time 20 \
        "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/process_cleanup/eboot.elf" || true
fi
echo "REBOOT REQUIRED: do not run a direct /dev/gc payload in this boot session" >&2
grep -q "FW 11.60 installed-driver workload oracle: PASS" "$output_file" || exit 1
if [ "$transport_status" -ne 0 ]; then
    echo "oracle passed; websrv transport ended with curl=$transport_status" >&2
fi
