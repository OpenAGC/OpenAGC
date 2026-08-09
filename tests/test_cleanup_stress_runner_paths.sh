#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

runner=${1:?usage: test_cleanup_stress_runner_paths.sh run-stress-script}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

printf 'stress bytes\n' > "$tmp/stress.elf"
printf 'cleanup bytes\n' > "$tmp/cleanup.elf"
sha=$(shasum -a 256 "$tmp/stress.elf" | awk '{print $1}')
mkdir -p "$tmp/bin"
cat > "$tmp/bin/curl" <<'MOCK_CURL'
#!/bin/sh
echo "curl must not run before the result-path gate" >&2
exit 99
MOCK_CURL
chmod +x "$tmp/bin/curl"

if PATH="$tmp/bin:$PATH" PS5_HOST=mock \
    STRESS_ARTIFACT="$tmp/stress.elf" \
    PROCESS_CLEANUP_ELF="$tmp/cleanup.elf" EXPECTED_FW_ABI=0x0550 \
    EXPECTED_ARTIFACT_SHA256="$sha" \
    RESULT_LOG_PATH=/data/homebrew/openagc_fw550_cleanup_stress/result.log \
    STRESS_ITERATIONS=1 sh "$runner" > "$tmp/output" 2>&1; then
    echo "cleanup-stress runner accepted a mismatched result path" >&2
    exit 1
fi
grep -q '^cleanup-stress result path mismatch$' "$tmp/output"
if grep -q 'curl must not run' "$tmp/output"; then
    echo "cleanup-stress runner contacted the console before path validation" >&2
    exit 1
fi

echo "PASS: cleanup-stress runner rejects divergent launch and verdict paths"
