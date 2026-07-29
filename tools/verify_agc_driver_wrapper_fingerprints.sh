#!/bin/sh
set -eu

firmware_root=${1:-/Volumes/Untitled/unp}
aerolib=${2:-/Users/bizkut/Downloads/PS5/sdk/sce_stubs/aerolib.csv}
tmp=${TMPDIR:-/tmp}/openagc-driver-wrapper-fingerprints.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

python3 tools/fingerprint_agc_driver_wrappers.py \
    "$firmware_root" --aerolib "$aerolib" --output "$tmp"
if ! cmp -s "$tmp" analysis/agc_driver_wrapper_fingerprints.tsv; then
    echo "FAIL: analysis/agc_driver_wrapper_fingerprints.tsv is stale" >&2
    diff -u analysis/agc_driver_wrapper_fingerprints.tsv "$tmp" >&2 || true
    exit 1
fi

profiles=$(awk -F '\t' '
    !/^#/ {
        count = split($4, keys, ",")
        for (i = 1; i <= count; i++) seen[keys[i]] = 1
    }
    END { for (key in seen) total++; print total + 0 }
' "$tmp")
if [ "$profiles" -ne 39 ]; then
    echo "FAIL: expected wrapper evidence for 39 active firmware keys, found $profiles" >&2
    exit 1
fi

if awk -F '\t' '!/^#/ && $2 == "MISSING" { found = 1 } END { exit !found }' "$tmp"; then
    echo "FAIL: one or more tracked wrappers are missing" >&2
    exit 1
fi

echo "PASS: all 39 AGC driver exported-wrapper groups are reproducible"
