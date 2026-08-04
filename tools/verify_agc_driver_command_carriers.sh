#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
tmp=${TMPDIR:-/tmp}/openagc-driver-command-carriers.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/fingerprint_agc_driver_command_carriers.py \
    "$firmware_root" --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_driver_command_carriers.tsv; then
    echo "FAIL: analysis/agc_driver_command_carriers.tsv is stale" >&2
    diff -u analysis/agc_driver_command_carriers.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all 39 AGC driver private-command carrier groups are reproducible"
