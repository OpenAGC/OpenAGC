#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Ordered FW 5.50 hardware qualification through etaHEN websrv.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PS5_HOST=${PS5_HOST:-10.0.1.41}
PS5_FTP_PORT=${PS5_FTP_PORT:-2121}
PS5_HTTP_PORT=${PS5_HTTP_PORT:-8080}
OPENAGC_CONFORMANCE_TIMEOUT=${OPENAGC_CONFORMANCE_TIMEOUT:-90}
RUN_ID=${OPENAGC_CONFORMANCE_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}
LOG_DIR=${OPENAGC_CONFORMANCE_LOG_DIR:-$SCRIPT_DIR/conformance-logs/$RUN_ID}

DEFAULT_SAMPLES="videoout_linear agc_init agc_videoout agc_compute agc_graphics agc_graphics_rgba8 agc_graphics_amplify agc_graphics_lines agc_graphics_invocations agc_tessellation agc_tess_geometry agc_tess_geometry_invocations agc_tess_geometry_lines agc_tess_geometry_rgba8 agc_depth_htile_ops agc_depth_expclear agc_depth_stencil_htile"
SAMPLES=$DEFAULT_SAMPLES
MODE=run

usage() {
    cat <<EOF
Usage: $0 [--check|--list] [--sample NAME]

Environment:
  PS5_HOST                         PS5 address (default: $PS5_HOST)
  PS5_FTP_PORT                     websrv FTP port (default: $PS5_FTP_PORT)
  PS5_HTTP_PORT                    websrv HTTP port (default: $PS5_HTTP_PORT)
  OPENAGC_CONFORMANCE_TIMEOUT      per-sample seconds (default: $OPENAGC_CONFORMANCE_TIMEOUT)
  OPENAGC_CONFORMANCE_LOG_DIR      persistent output directory
  OPENAGC_CONFORMANCE_RUN_ID       remote-path/log run identifier

--check validates the local matrix without contacting a PS5.
--list prints the ordered sample names.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --check) MODE=check ;;
        --list) MODE=list ;;
        --sample)
            shift
            [ "$#" -gt 0 ] || { echo "--sample requires a name" >&2; exit 2; }
            SAMPLES=$1
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

artifact_for() {
    case "$1" in
        videoout_linear|agc_init|agc_videoout|agc_compute|agc_graphics|\
        agc_graphics_rgba8|agc_graphics_amplify|agc_graphics_lines|\
        agc_graphics_invocations|agc_tessellation|agc_tess_geometry|\
        agc_tess_geometry_invocations|agc_tess_geometry_lines|\
        agc_tess_geometry_rgba8|agc_depth_htile_ops|agc_depth_expclear|\
        agc_depth_stencil_htile)
            printf '%s/%s.elf\n' "$SCRIPT_DIR" "$1"
            ;;
        *) return 1 ;;
    esac
}

require_log() {
    pattern=$1
    log=$2
    label=$3
    if ! grep -Eq "$pattern" "$log"; then
        echo "  missing gate: $label" >&2
        return 1
    fi
}

require_graphics_log() {
    sample=$1
    log=$2
    require_log '\[Wave\] records:.*PASS' "$log" 'Wave32 shader records' || return
    require_log 'GPU completion fence reached' "$log" 'EOP completion fence' || return
    case "$sample" in
        *rgba8)
            require_log 'Interleaved buffer fetch: PASS' "$log" 'vertex fetch' || return
            require_log 'Bound u16 indexed draw: PASS' "$log" 'indexed draw' || return
            require_log 'gfx1013 image \+ bilinear sampler: PASS' "$log" 'texture sampling' || return
            ;;
        *)
            require_log 'GFX1013 .*FP16 target: PASS' "$log" 'FP16 target' || return
            ;;
    esac
    require_log 'VideoOut sustained preview: 1800 accepted, 1800 completed' "$log" '1800-frame preview'
}

validate_log() {
    sample=$1
    log=$2

    if grep -Eq 'FATAL|FAIL|MISMATCH|timed out|kernel panic' "$log"; then
        echo "  failure marker found in output" >&2
        return 1
    fi

    case "$sample" in
        videoout_linear)
            require_log 'VideoOut sustained smoke: 600/600 flips completed' "$log" '600 VideoOut flips'
            return
            ;;
        *)
            require_log 'system software raw=0x0550[0-9A-Fa-f]{4}' "$log" 'numeric FW 0x0550 profile' || return
            ;;
    esac

    case "$sample" in
        agc_init)
            require_log 'Runtime profile:[[:space:]]+FW ABI 0x0550 PASS' "$log" 'FW 5.50 runtime profile' || return
            require_log '=== Done ===' "$log" 'completed init lifecycle'
            ;;
        agc_videoout)
            require_log 'GPU NOP submit:[[:space:]]+tested' "$log" 'NOP submission' || return
            require_log 'Color bars rendered: 600 frames' "$log" '600 color-bar frames'
            ;;
        agc_compute)
            require_log 'GPU completion fence reached' "$log" 'EOP completion fence' || return
            require_log 'Total: 2073600/2073600 pixels match' "$log" 'complete compute output' || return
            require_log 'GPU output flip completed' "$log" 'compute display flip'
            ;;
        agc_depth_htile_ops)
            require_log '\[Depth Marker\] stage\[3\]=0xd3200004 expected=0xd3200004' "$log" 'four depth stages' || return
            require_log '\[HTILE Readback\] changed=[1-9][0-9]* .*initial=fffc000f' "$log" 'resummarized depth HTILE' || return
            require_log 'raw D32: one=909792 near=128304 far=128304' "$log" 'decompressed D32 classes' || return
            require_log '\[Depth Result\] markers=PASS color=PASS raw-depth=PASS stencil=PASS' "$log" 'depth HTILE operation result' || return
            require_log 'VideoOut sustained preview: 1800 accepted, 1800 completed' "$log" '1800-frame preview'
            ;;
        agc_depth_expclear)
            require_log '\[Depth\+Expclear\] emitted metadata clear' "$log" 'metadata-only depth initialization' || return
            require_log '\[HTILE Readback\] changed=49152 .*initial=fffffff0' "$log" 'depth-one expclear metadata' || return
            require_log 'raw D32: one=918432 near=128304 far=128304' "$log" 'expanded expclear D32 classes' || return
            require_log '\[Depth Result\] markers=PASS color=PASS raw-depth=PASS stencil=PASS' "$log" 'depth expclear result' || return
            require_log 'VideoOut sustained preview: 1800 accepted, 1800 completed' "$log" '1800-frame preview'
            ;;
        agc_depth_stencil_htile)
            require_log '\[HTILE Readback\] changed=49152 .*initial=fffff30f' "$log" 'combined resummarized HTILE' || return
            require_log 'raw D32: one=909792 near=128304 far=128304' "$log" 'combined decompressed D32 classes' || return
            require_log '\[Stencil Readback\] zero=2364832 replace-5a=256608 other=0' "$log" 'combined decompressed S8 classes' || return
            require_log '\[Depth\+Stencil Result\] markers=PASS color=PASS raw-depth=PASS stencil=PASS' "$log" 'combined stencil HTILE result' || return
            require_log 'VideoOut sustained preview: 1800 accepted, 1800 completed' "$log" '1800-frame preview'
            ;;
        agc_graphics*)
            require_graphics_log "$sample" "$log"
            ;;
        agc_tess*)
            require_log 'TF-ring address setup: 0x00000000' "$log" 'TF-ring setup' || return
            require_log 'reusable gfx1013 HS\+TES\+PS bind: 0x00000000' "$log" 'tessellation binder' || return
            require_graphics_log "$sample" "$log"
            ;;
    esac
}

if [ "$MODE" = list ]; then
    for sample in $SAMPLES; do echo "$sample"; done
    exit 0
fi

for sample in $SAMPLES; do
    if ! artifact=$(artifact_for "$sample"); then
        echo "unknown conformance sample: $sample" >&2
        exit 2
    fi
    if [ ! -s "$artifact" ]; then
        echo "missing artifact: $artifact" >&2
        exit 2
    fi
done

if [ "$MODE" = check ]; then
    echo "FW 5.50 conformance matrix: local artifacts OK"
    for sample in $SAMPLES; do echo "  $sample"; done
    exit 0
fi

mkdir -p "$LOG_DIR" || exit 2
revision=$(git -C "$SCRIPT_DIR/../.." rev-parse --verify HEAD 2>/dev/null || printf unknown)
printf 'run_id=%s\nfw=0x0550\nhost=%s\ntimeout=%s\nrevision=%s\n' \
    "$RUN_ID" "$PS5_HOST" "$OPENAGC_CONFORMANCE_TIMEOUT" "$revision" \
    > "$LOG_DIR/run.env"
: > "$LOG_DIR/artifacts.sha256"
: > "$LOG_DIR/logs.sha256"
for sample in $SAMPLES; do
    artifact=$(artifact_for "$sample")
    shasum -a 256 "$artifact" >> "$LOG_DIR/artifacts.sha256" || exit 2
done

passed=0
total=0
for sample in $SAMPLES; do
    total=$((total + 1))
    artifact=$(artifact_for "$sample")
    log="$LOG_DIR/$sample.log"
    remote_dir="/data/homebrew/openagc_fw550_${RUN_ID}_${sample}"

    echo "[$total] $sample"
    if ! curl -sS --fail --ftp-create-dirs -T "$artifact" \
        "ftp://$PS5_HOST:$PS5_FTP_PORT$remote_dir/eboot.elf"; then
        echo "  upload failed; stopping before launch" >&2
        exit 1
    fi

    started=$(date +%s)
    curl -sS --fail --max-time "$OPENAGC_CONFORMANCE_TIMEOUT" \
        "http://$PS5_HOST:$PS5_HTTP_PORT/hbldr?pipe=1&daemon=0&path=$remote_dir/eboot.elf" \
        > "$log" 2>&1
    curl_status=$?
    elapsed=$(($(date +%s) - started))

    cat "$log"
    if [ "$curl_status" -ne 0 ]; then
        echo "  launch transport failed (curl=$curl_status, elapsed=${elapsed}s); stopping" >&2
        exit 1
    fi
    if ! validate_log "$sample" "$log"; then
        echo "  conformance gates failed (elapsed=${elapsed}s); stopping" >&2
        exit 1
    fi
    shasum -a 256 "$log" >> "$LOG_DIR/logs.sha256" || exit 2

    passed=$((passed + 1))
    echo "  PASS ($elapsed seconds)"
done

echo "FW 5.50 conformance: $passed/$total samples passed"
echo "Logs: $LOG_DIR"
