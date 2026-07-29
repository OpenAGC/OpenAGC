#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
tmp=${TMPDIR:-/tmp}/openagc-driver-memory-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/extract_agc_driver_memory_facts.py "$firmware_root" \
    --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_driver_memory_facts.tsv; then
    echo "FAIL: analysis/agc_driver_memory_facts.tsv is stale" >&2
    diff -u analysis/agc_driver_memory_facts.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all 39 AGC driver internal-memory facts are reproducible"
