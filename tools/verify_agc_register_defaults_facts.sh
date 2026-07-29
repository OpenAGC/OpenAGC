#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
tmp=${TMPDIR:-/tmp}/openagc-register-defaults-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/extract_agc_register_defaults_facts.py "$firmware_root" \
    --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_register_defaults_facts.tsv; then
    echo "FAIL: analysis/agc_register_defaults_facts.tsv is stale" >&2
    diff -u analysis/agc_register_defaults_facts.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all 39 libSceAgc register-default selectors are reproducible"
