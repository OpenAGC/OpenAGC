#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
tmp=${TMPDIR:-/tmp}/openagc-videoout-patch-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/extract_videoout_patch_facts.py "$firmware_root" \
    --output "$tmp"
if ! cmp -s "$tmp" analysis/videoout_linear_patch_facts.tsv; then
    echo "FAIL: analysis/videoout_linear_patch_facts.tsv is stale" >&2
    diff -u analysis/videoout_linear_patch_facts.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all active VideoOut linear-patch facts are reproducible"
