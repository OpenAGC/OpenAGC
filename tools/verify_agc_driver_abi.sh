#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 path/to/libSceAgcDriver.sprx" >&2
    exit 2
fi

sprx=$1
objdump=${OBJDUMP:-llvm-objdump}
tmp=${TMPDIR:-/tmp}/openagc-agc-driver-abi.$$
trap 'rm -f "$tmp"' EXIT HUP INT TERM

"$objdump" -d "$sprx" >"$tmp"

require()
{
    label=$1
    pattern=$2
    if ! grep -Eiq "$pattern" "$tmp"; then
        echo "FAIL: missing $label ($pattern)" >&2
        exit 1
    fi
}

require "context query" 'c004812e'
require "16-byte submit" 'c0108102'
require "PID/direct submit" 'c010813b'
require "queue create" 'c0408121'
require "queue destroy" 'c00c810e'
require "async graphics setup" '80048126'
require "TF ring" 'c0108139'
require "HS offchip parameter" 'c008812d'
require "queue token 0" 'af1e80b7'
require "queue token 1" '8b4cdd90'
require "queue token 2" '99f68d6c'
require "queue token 3" 'e5fcc174'
require "standard CWSR size" '1000000'
require "DDID size" 'fc000'
require "EOP FIFO size" '3c000'
require "ACQRB size" '1e0000'
require "EOP ring offset" '39000'
require "ACQRB read-pointer offset" '1c8000'
require "ACQRB metadata offset" '1cc000'

echo "PASS: $sprx matches the OpenAGC standard direct-submit ABI facts"
