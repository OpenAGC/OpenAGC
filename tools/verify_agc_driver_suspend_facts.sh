#!/bin/sh
set -eu

tmp=${TMPDIR:-/tmp}/openagc-driver-suspend-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/build_agc_driver_suspend_facts.py --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_driver_suspend_facts.tsv; then
    echo "FAIL: analysis/agc_driver_suspend_facts.tsv is stale" >&2
    diff -u analysis/agc_driver_suspend_facts.tsv "$tmp" >&2 || true
    exit 1
fi

echo "PASS: all 39 AGC driver suspend facts are reproducible"
