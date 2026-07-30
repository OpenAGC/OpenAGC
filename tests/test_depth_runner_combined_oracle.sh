#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

runner=${1:?usage: test_depth_runner_combined_oracle.sh run-depth-script}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

printf 'combined depth bytes\n' > "$tmp/depth.elf"
printf 'cleanup bytes\n' > "$tmp/cleanup.elf"
cat > "$tmp/result.log" <<'RESULT'
Runtime profile FW ABI 0x1160: PASS
GPU completion fence reached
[Combined Expclear RMW] aspects=0x1 gate=ON offset=0x0 size=0x1000 selected=1024 expected=fffffff0 mismatch=0 outside-changed=0 reserved=PASS fence=48544c45: PASS
[HTILE Readback] changed=1024 other=0 initial=fffff30f
[Depth Readback] green=228096 red=228096 left=ff00ff00 right=ffff0000
[Depth Readback] raw D32: one=1617408 near=228096 far=228096
[Stencil Readback] zero=1617408 replace-5a=456192 other=0
[Depth+Stencil Result] markers=PASS color=PASS raw-depth=PASS stencil=PASS
Driver shutdown: PASS
Graphics result: PASS
RESULT

mkdir -p "$tmp/bin"
cat > "$tmp/bin/curl" <<'MOCK_CURL'
#!/bin/sh
set -eu
output=
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-o" ]; then
        output=$2
        shift 2
    else
        shift
    fi
done
if [ -n "$output" ]; then
    cp "$MOCK_RESULT" "$output"
fi
MOCK_CURL
cat > "$tmp/bin/sleep" <<'MOCK_SLEEP'
#!/bin/sh
exit 0
MOCK_SLEEP
chmod +x "$tmp/bin/curl" "$tmp/bin/sleep"

run_gate()
{
    PATH="$tmp/bin:$PATH" MOCK_RESULT="$tmp/result.log" PS5_HOST=mock \
        DEPTH_ARTIFACT="$tmp/depth.elf" \
        PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" \
        RESULT_LOG_PATH=/data/homebrew/openagc_fw1160_depth/result.log \
        EXPECTED_HTILE_INITIAL=fffff30f EXPECTED_D32_FULL_RECT=1 \
        EXPECTED_STENCIL_FULL_RECT=1 \
        EXPECTED_COMBINED_EXPCLEAR_ASPECTS="$1" \
        sh "$runner"
}

run_gate 1 > "$tmp/pass-output"
if run_gate 2 > "$tmp/fail-output" 2>&1; then
    echo "depth runner accepted the wrong combined expclear aspect" >&2
    exit 1
fi

echo "PASS: depth runner enforces exact combined depth/stencil verdicts"
