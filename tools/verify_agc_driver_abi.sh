#!/bin/sh
set -eu

if [ "$#" -eq 1 ]; then
    family=standard
    sprx=$1
elif [ "$#" -eq 2 ]; then
    family=$1
    sprx=$2
else
    echo "usage: $0 [legacy-v1|legacy-v2|legacy-v3|standard] path/to/libSceAgcDriver.sprx" >&2
    exit 2
fi

case $family in
    legacy-v1|legacy-v2|legacy-v3|standard) ;;
    *) echo "unknown ABI family: $family" >&2; exit 2 ;;
esac

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

require_absent()
{
    label=$1
    pattern=$2
    if grep -Eiq "$pattern" "$tmp"; then
        echo "FAIL: unexpected $label ($pattern)" >&2
        exit 1
    fi
}

require "context query" 'c004812e'
require "16-byte submit" 'c0108102'
require "queue create" 'c0408121'
require "queue destroy" 'c00c810e'
require "async graphics setup" '80048126'
require "standard CWSR size" '1000000'
require "DDID size" 'fc000'
require "EOP FIFO size" '3c000'
require "ACQRB size" '1e0000'

case $family in
    legacy-v1)
        require_absent "later queue token" 'af1e80b7'
        require "legacy EOP ring offset" '38000'
        ;;
    legacy-v2)
        require "EOP ring offset" '39000'
        ;;
    legacy-v3)
        require "EOP ring offset" '39000'
        ;;
    standard)
        require "public TF ring" '80108128'
        require "privileged TF ring" 'c0108120'
        require "HS offchip parameter" 'c010812c'
        require "final suspend" 'c0108139'
        require "EOP ring offset" '39000'
        ;;
esac

if [ "$family" != legacy-v1 ]; then
    require "queue token 0" 'af1e80b7'
    require "queue token 1" '8b4cdd90'
    require "queue token 2" '99f68d6c'
    require "queue token 3" 'e5fcc174'
fi

if [ "$family" = standard ]; then
    require "ACQRB read-pointer offset" '1c8000'
    require "ACQRB metadata offset" '1cc000'
fi

echo "PASS: $sprx matches the OpenAGC $family submit16 ABI facts"
