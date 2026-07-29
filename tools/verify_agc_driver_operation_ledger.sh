#!/bin/sh
set -eu

tmp=${TMPDIR:-/tmp}/openagc-driver-operation-facts.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/build_agc_driver_operation_ledger.py --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_driver_operation_facts.tsv; then
    echo "FAIL: analysis/agc_driver_operation_facts.tsv is stale" >&2
    diff -u analysis/agc_driver_operation_facts.tsv "$tmp" >&2 || true
    exit 1
fi

rows=$(awk 'BEGIN { count = 0 } !/^#/ { count++ } END { print count }' "$tmp")
if [ "$rows" -ne 39 ]; then
    echo "FAIL: expected 39 active firmware rows, found $rows" >&2
    exit 1
fi

echo "PASS: 39-key AGC driver operation ledger is reproducible"
