#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -u

PS5_HOST=${PS5_HOST:?set PS5_HOST}
STAGE=${STAGE:?set STAGE}
STAGE_ARTIFACT=${STAGE_ARTIFACT:?set STAGE_ARTIFACT}
PROCESS_CLEANUP_ELF=${PROCESS_CLEANUP_ELF:?set PROCESS_CLEANUP_ELF}
REMOTE_BASE=/data/homebrew/openagc_fw1160_stage${STAGE}

if [ ! -s "$STAGE_ARTIFACT" ] || [ ! -s "$PROCESS_CLEANUP_ELF" ]; then
    echo "missing stage or process-cleanup ELF" >&2
    exit 2
fi

# The cleanup payload must be the immediately preceding homebrew launch.
curl -sS --fail --ftp-create-dirs -T "$PROCESS_CLEANUP_ELF" \
    "ftp://$PS5_HOST:2121/data/homebrew/process_cleanup/eboot.elf" || exit 1
curl -sS --max-time 20 \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/process_cleanup/eboot.elf" || exit 1
sleep 2

curl -sS --fail --ftp-create-dirs -T "$STAGE_ARTIFACT" \
    "ftp://$PS5_HOST:2121$REMOTE_BASE/eboot.elf" || exit 1
output=$(curl -sS --fail --max-time 20 \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=$REMOTE_BASE/eboot.elf") || exit 1
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -q "stage $STAGE: PASS" || exit 1
