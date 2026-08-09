#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

runner=${1:?usage: test_runtime_guarded_runner.sh run-runtime-script}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

printf 'runtime bytes\n' > "$tmp/runtime.elf"
printf 'cleanup bytes\n' > "$tmp/cleanup.elf"
sha=$(shasum -a 256 "$tmp/runtime.elf" | awk '{print $1}')
mkdir -p "$tmp/bin"
cat > "$tmp/bin/curl" <<'MOCK_CURL'
#!/bin/sh
case "$*" in
    *'hbldr?pipe=1&daemon=0'*)
        echo 'Runtime profile: mock-standard (FW ABI 0x0550)'
        echo 'agcDestroyDevice: 0x00000000 (AGC_OK)'
        echo 'PRESENT_STAGE_0 PASS'
        ;;
esac
exit 0
MOCK_CURL
chmod +x "$tmp/bin/curl"

run_gate()
{
    PATH="$tmp/bin:$PATH" PS5_HOST=mock \
        RUNTIME_ARTIFACT="$tmp/runtime.elf" \
        PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" EXPECTED_FW_ABI=0x0550 \
        EXPECTED_ARTIFACT_SHA256="$1" EXPECTED_VERDICT="$2" \
        REMOTE_NAME=runtime_stage0 RUN_TIMEOUT_SECONDS=1 \
        sh "$runner"
}

run_gate "$sha" 'PRESENT_STAGE_0 PASS' > "$tmp/success" 2>&1
grep -q '^Guarded runtime gate: PRESENT_STAGE_0 PASS$' "$tmp/success"

if run_gate 0000000000000000000000000000000000000000000000000000000000000000 \
        'PRESENT_STAGE_0 PASS' > "$tmp/hash" 2>&1; then
    echo "guarded runner accepted a divergent artifact hash" >&2
    exit 1
fi
grep -q '^runtime-gate artifact SHA-256 mismatch$' "$tmp/hash"

if run_gate "$sha" 'PRESENT_STAGE_1 PASS' > "$tmp/verdict" 2>&1; then
    echo "guarded runner accepted a missing verdict" >&2
    exit 1
fi

echo "PASS: guarded runtime runner pins artifact and verdict"
