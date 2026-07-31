#!/bin/sh
set -eu

cmake=$1
package_build=$2
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/openagc-installed-examples.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

"$cmake" --install "$package_build" --prefix "$tmpdir/prefix"
"$cmake" -S "$tmpdir/prefix/share/doc/OpenAGC/examples" \
    -B "$tmpdir/build" -DCMAKE_PREFIX_PATH="$tmpdir/prefix"
"$cmake" --build "$tmpdir/build" --parallel
"$tmpdir/build/openagc_first_compute"
"$tmpdir/build/openagc_first_triangle"

echo "installed-package examples: PASS"
