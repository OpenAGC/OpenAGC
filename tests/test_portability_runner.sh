#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

runner=${1:?usage: test_portability_runner.sh path/to/run_portability.sh}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

mock_bin=$tmp/bin
remote=$tmp/remote
log=$tmp/curl.log
mkdir -p "$mock_bin" "$remote"
printf 'pinned portability bytes\n' > "$tmp/portability.elf"
printf 'pinned cleanup bytes\n' > "$tmp/cleanup.elf"
printf 'pinned firmware probe bytes\n' > "$tmp/probe.elf"
artifact_sha=$(shasum -a 256 "$tmp/portability.elf" | awk '{print $1}')
cleanup_sha=$(shasum -a 256 "$tmp/cleanup.elf" | awk '{print $1}')
probe_sha=$(shasum -a 256 "$tmp/probe.elf" | awk '{print $1}')

cat > "$mock_bin/curl" <<'MOCK_CURL'
#!/bin/sh
set -eu

upload=
output=
quote=
url=
while [ "$#" -gt 0 ]; do
    case "$1" in
        -T) upload=$2; shift 2 ;;
        -o) output=$2; shift 2 ;;
        --quote) quote=$2; shift 2 ;;
        --max-time) shift 2 ;;
        -sS|--fail|--ftp-create-dirs) shift ;;
        *) url=$1; shift ;;
    esac
done

if [ -n "$quote" ]; then
    path=${quote#DELE }
    rm -f "$MOCK_REMOTE$path"
    exit 0
fi

case "$url" in
    ftp://*)
        path=/${url#*://*/}
        target=$MOCK_REMOTE$path
        if [ -n "$upload" ]; then
            mkdir -p "${target%/*}"
            cp "$upload" "$target"
            if [ "${MOCK_CORRUPT_REMOTE:-}" = cleanup ] &&
                    [ "$path" = /data/homebrew/openagc_process_cleanup/eboot.elf ]; then
                printf 'corrupt\n' >> "$target"
            fi
            printf 'upload %s\n' "$path" >> "$MOCK_LOG"
        elif [ -n "$output" ] && [ -f "$target" ]; then
            cp "$target" "$output"
        else
            exit 22
        fi
        ;;
    http://*/hbldr*)
        path=${url##*path=}
        printf 'launch %s\n' "$path" >> "$MOCK_LOG"
        case "$path" in
            */firmware_probe.elf)
                mkdir -p "$MOCK_REMOTE/data/homebrew/openagc_portability"
                {
                    printf 'Firmware preflight raw=0x%s0005 key=0x%s string=test\n' \
                        "$MOCK_FW_KEY" "$MOCK_FW_KEY"
                    printf 'Firmware preflight result: PASS\n'
                } > "$MOCK_REMOTE/data/homebrew/openagc_portability/preflight.log"
                ;;
            /data/homebrew/openagc_portability/eboot.elf)
                mkdir -p "$MOCK_REMOTE/data/homebrew/openagc_portability"
                {
                    printf 'Runtime profile raw=0x%s0005 key=0x%s family=standard model=standard-ps5: PASS\n' \
                        "$MOCK_FW_KEY" "$MOCK_FW_KEY"
                    printf 'VideoOut driver defaults/async: PASS/PASS\n'
                    printf 'Public VideoOut open/register/restore: 0x00000000 PASS\n'
                    printf 'VideoOut GPU marker: value=0x504f5254 wait=50 ms PASS\n'
                    printf 'Public VideoOut bounded flips: PASS\n'
                    printf 'Public VideoOut cleanup: shutdown=0x00000000 submit=0x00000000 unmap=0x00000000 direct=0x00000000\n'
                    printf 'Portability result: PASS\n'
                } > "$MOCK_REMOTE/data/homebrew/openagc_portability/result.log"
                ;;
        esac
        ;;
    http://*) ;;
    *) exit 22 ;;
esac
MOCK_CURL
chmod +x "$mock_bin/curl"

cat > "$mock_bin/sleep" <<'MOCK_SLEEP'
#!/bin/sh
exit 0
MOCK_SLEEP
chmod +x "$mock_bin/sleep"

run_runner()
{
    expected_artifact=$1
    expected_cleanup=$2
    expected_probe=$3
    fw_key=$4
    corrupt=${5:-}

    PATH=$mock_bin:$PATH MOCK_REMOTE=$remote MOCK_LOG=$log \
    MOCK_FW_KEY=$fw_key MOCK_CORRUPT_REMOTE=$corrupt \
    PS5_HOST=mock PORTABILITY_ARTIFACT=$tmp/portability.elf \
    PROCESS_CLEANUP_ELF=$tmp/cleanup.elf FIRMWARE_PROBE_ELF=$tmp/probe.elf \
    EXPECTED_ARTIFACT_SHA256=$expected_artifact \
    EXPECTED_CLEANUP_SHA256=$expected_cleanup \
    EXPECTED_PROBE_SHA256=$expected_probe \
    EXPECTED_FIRMWARE_ABI_KEY=0550 PORTABILITY_ITERATIONS=2 \
    sh "$runner" >/dev/null 2>&1
}

assert_status()
{
    expected=$1
    shift
    : > "$log"
    set +e
    run_runner "$@"
    actual=$?
    set -e
    if [ "$actual" -ne "$expected" ]; then
        echo "runner status mismatch: expected=$expected actual=$actual" >&2
        exit 1
    fi
}

bad_sha=0000000000000000000000000000000000000000000000000000000000000000
assert_status 2 "$bad_sha" "$cleanup_sha" "$probe_sha" 0550
[ ! -s "$log" ] || { echo "bad payload hash reached transport" >&2; exit 1; }

assert_status 2 "$artifact_sha" "$bad_sha" "$probe_sha" 0550
[ ! -s "$log" ] || { echo "bad cleanup hash reached transport" >&2; exit 1; }

assert_status 1 "$artifact_sha" "$cleanup_sha" "$probe_sha" 0550 cleanup
if grep -q '^launch ' "$log"; then
    echo "corrupted uploaded cleanup reached launch" >&2
    exit 1
fi

assert_status 1 "$artifact_sha" "$cleanup_sha" "$probe_sha" 1160
grep -q '^launch /data/homebrew/openagc_portability/firmware_probe.elf$' "$log"
if grep -q '^upload /data/homebrew/openagc_portability/eboot.elf$' "$log"; then
    echo "wrong firmware reached payload upload" >&2
    exit 1
fi

assert_status 0 "$artifact_sha" "$cleanup_sha" "$probe_sha" 0550
[ "$(grep -c '^launch /data/homebrew/openagc_process_cleanup/eboot.elf$' "$log")" -eq 3 ]
[ "$(grep -c '^launch /data/homebrew/openagc_portability/eboot.elf$' "$log")" -eq 2 ]

echo "PASS: portability runner fails closed and preserves cleanup ordering"
