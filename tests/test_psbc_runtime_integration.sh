#!/bin/sh
set -eu

test_binary=$1
compiler=${GLSLANG_VALIDATOR:-glslangValidator}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/openagc-psbc-runtime.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

"$compiler" -V "$root/../openagc-psbc/tests/varying.vert" \
    -o "$tmpdir/vertex.spv" >/dev/null
"$compiler" -V "$root/../openagc-psbc/tests/library_spec.frag" \
    -o "$tmpdir/fragment.spv" >/dev/null
"$compiler" -V "$root/../openagc-psbc/tests/library_resources.comp" \
    -o "$tmpdir/compute.spv" >/dev/null
"$test_binary" "$tmpdir/vertex.spv" "$tmpdir/fragment.spv" \
    "$tmpdir/compute.spv"
