#!/bin/sh
set -eu

cmake=$1
package_build=$2
tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/openagc-installed-examples.XXXXXX")
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

"$cmake" --install "$package_build" --prefix "$tmpdir/prefix"
for guide in getting_started api_reference capabilities_debugging \
    native_runtime memory_resources shader_pipelines validation capture
do
    test -f "$tmpdir/prefix/share/doc/OpenAGC/$guide.md"
done
"$cmake" -S "$tmpdir/prefix/share/doc/OpenAGC/examples" \
    -B "$tmpdir/build" -DCMAKE_PREFIX_PATH="$tmpdir/prefix"
"$cmake" --build "$tmpdir/build" --parallel
"$tmpdir/build/openagc_first_compute"
"$tmpdir/build/openagc_first_triangle"

echo "installed-package examples: PASS"
