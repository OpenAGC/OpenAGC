#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the PS5 address}"
ELF=${1:-build/openagc_cube.elf}
APP_DIR=/data/homebrew/openagc_cube

test -f "$ELF"
curl -s "ftp://${PS5_HOST}:2121/" --quote "MKD ${APP_DIR}" >/dev/null 2>&1 || true
curl -T "$ELF" "ftp://${PS5_HOST}:2121${APP_DIR}/eboot.elf"
curl -s "http://${PS5_HOST}:8080/hbldr?pipe=1&daemon=0&path=${APP_DIR}/eboot.elf"

