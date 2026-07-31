#!/bin/sh
set -eu

reference=$1
decoder=$2
python=$3
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/openagc-reference-capture.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

"$reference" "$tmpdir/first.oagc"
"$reference" "$tmpdir/second.oagc"
"$python" "$decoder" "$tmpdir/first.oagc" >"$tmpdir/first.txt"
"$python" "$decoder" "$tmpdir/second.oagc" >"$tmpdir/second.txt"

cmp "$tmpdir/first.txt" "$tmpdir/second.txt"

for record in RUNTIME_INFO RESOURCE_DESC SHADER_DESC PIPELINE_DESC \
    RESOURCE_TRANSITION COMMAND_BEGIN COMMAND_END COMMAND_STREAM SUBMISSION \
    FENCE_RESULT READBACK_HASH END
do
    grep -q "$record" "$tmpdir/first.txt"
done

grep -q 'reference-output' "$tmpdir/first.txt"
grep -q 'reference-frame' "$tmpdir/first.txt"
grep -q 'DISPATCH_DIRECT' "$tmpdir/first.txt"
grep -q 'algorithm=fnv1a64' "$tmpdir/first.txt"
grep -q '<redacted>' "$tmpdir/first.txt"

echo "deterministic reference capture: PASS"
