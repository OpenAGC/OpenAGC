#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Run one exact-firmware direct-/dev/gc probe through etaHEN websrv.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PS5_HOST=${PS5_HOST:?set PS5_HOST to the target console address}
PS5_FTP_PORT=${PS5_FTP_PORT:-2121}
PS5_HTTP_PORT=${PS5_HTTP_PORT:-8080}
EXPECTED_FW_ABI=${EXPECTED_FW_ABI:?set EXPECTED_FW_ABI, for example 1160}
PROBE_ARTIFACT=${PROBE_ARTIFACT:-$SCRIPT_DIR/agc_init_fw${EXPECTED_FW_ABI}.elf}
OPENAGC_CONFORMANCE_TIMEOUT=${OPENAGC_CONFORMANCE_TIMEOUT:-90}
RUN_ID=${OPENAGC_CONFORMANCE_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}
LOG_DIR=${OPENAGC_CONFORMANCE_LOG_DIR:-$SCRIPT_DIR/conformance-logs/$RUN_ID}
REMOTE_DIR=/data/homebrew/openagc_fw${EXPECTED_FW_ABI}_${RUN_ID}_direct

if [ ! -s "$PROBE_ARTIFACT" ]; then
    echo "missing probe artifact: $PROBE_ARTIFACT" >&2
    exit 2
fi

mkdir -p "$LOG_DIR" || exit 2
revision=$(git -C "$SCRIPT_DIR/../.." rev-parse --verify HEAD 2>/dev/null || printf unknown)
printf 'run_id=%s\nfw=0x%s\nhost=%s\ntimeout=%s\nrevision=%s\n' \
    "$RUN_ID" "$EXPECTED_FW_ABI" "$PS5_HOST" \
    "$OPENAGC_CONFORMANCE_TIMEOUT" "$revision" > "$LOG_DIR/run.env"
shasum -a 256 "$PROBE_ARTIFACT" > "$LOG_DIR/artifacts.sha256" || exit 2

curl -sS --fail --ftp-create-dirs -T "$PROBE_ARTIFACT" \
    "ftp://$PS5_HOST:$PS5_FTP_PORT$REMOTE_DIR/eboot.elf" || exit 1

LOG=$LOG_DIR/agc_init_fw${EXPECTED_FW_ABI}.log
curl -sS --fail --max-time "$OPENAGC_CONFORMANCE_TIMEOUT" \
    "http://$PS5_HOST:$PS5_HTTP_PORT/hbldr?pipe=1&daemon=0&path=$REMOTE_DIR/eboot.elf" \
    > "$LOG" 2>&1
status=$?
cat "$LOG"
if [ "$status" -ne 0 ]; then
    echo "direct probe transport failed: curl=$status" >&2
    exit 1
fi

if grep -Eq 'FATAL|FAIL|MISMATCH|timed out|kernel panic' "$LOG"; then
    echo "direct probe reported a failure marker" >&2
    exit 1
fi

require() {
    pattern=$1
    label=$2
    if ! grep -Eq "$pattern" "$LOG"; then
        echo "missing direct-probe gate: $label" >&2
        exit 1
    fi
}

require "system software raw=0x${EXPECTED_FW_ABI}[0-9A-Fa-f]{4}" 'exact firmware'
require "Runtime profile:[[:space:]]+FW ABI 0x${EXPECTED_FW_ABI} PASS" 'runtime profile'
require 'Batched DCBs:[[:space:]]+OK' 'multi-DCB execution'
require '9-dword wait64:[[:space:]]+PASS' 'native wait64 execution'
require 'Default states:[[:space:]]+PASS' 'default-state capability contract'
require 'Async graphics:[[:space:]]+PASS' 'async setup'
require 'Queue contract:[[:space:]]+PASS' 'queue lifecycle'
require 'Suspend point:[[:space:]]+PASS' 'suspend submission'
require 'Workload contract:[[:space:]]+PASS' 'workload capability contract'
require '=== Done ===' 'complete lifecycle'

shasum -a 256 "$LOG" > "$LOG_DIR/logs.sha256" || exit 2
echo "FW 0x$EXPECTED_FW_ABI direct /dev/gc probe: PASS"
echo "Logs: $LOG_DIR"
