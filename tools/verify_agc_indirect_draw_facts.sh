#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
tmp=${TMPDIR:-/tmp}/openagc-indirect-draw-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/extract_agc_indirect_draw_facts.py "$firmware_root" \
    --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_indirect_draw_facts.tsv; then
    echo "FAIL: analysis/agc_indirect_draw_facts.tsv is stale" >&2
    diff -u analysis/agc_indirect_draw_facts.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all 39 AGC indirect-draw ABI facts are reproducible"
