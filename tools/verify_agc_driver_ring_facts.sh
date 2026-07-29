#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
tmp=${TMPDIR:-/tmp}/openagc-driver-ring-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/extract_agc_driver_ring_facts.py "$firmware_root" \
    --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_driver_ring_facts.tsv; then
    echo "FAIL: analysis/agc_driver_ring_facts.tsv is stale" >&2
    diff -u analysis/agc_driver_ring_facts.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all 39 TF-ring/HS-offchip payload facts are reproducible"
