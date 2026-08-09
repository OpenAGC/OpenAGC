#!/bin/sh
set -eu

PSBC=${PSBC:-../../../openagc-psbc/psbc}
OUT=${TMPDIR:-/tmp}/openagc-cube-shaders
mkdir -p "$OUT"

glslangValidator -V --target-env vulkan1.2 shaders/cube.vert.glsl \
    -o "$OUT/cube.vert.spv"
glslangValidator -V --target-env vulkan1.2 shaders/cube.geom.glsl \
    -o "$OUT/cube.geom.spv"
glslangValidator -V --target-env vulkan1.2 shaders/cube.frag.glsl \
    -o "$OUT/cube.frag.spv"

"$PSBC" -f "$OUT/cube.geom.spv" -s geometry \
    --pre-vertex "$OUT/cube.vert.spv" --wave32 \
    --vertex-attribute 0:0:0:24:r32g32b32_float \
    --vertex-attribute 1:0:12:24:r32g32b32_float \
    --ngg-front "$OUT/cube_ngg_front.sb" \
    -o "$OUT/cube_ngg_back.sb"
"$PSBC" -f "$OUT/cube.frag.spv" -s fragment \
    -o "$OUT/cube_frag.sb"

xxd -i -n cube_ngg_front_data "$OUT/cube_ngg_front.sb" \
    shaders/cube_ngg_front_sb.h
xxd -i -n cube_ngg_back_data "$OUT/cube_ngg_back.sb" \
    shaders/cube_ngg_back_sb.h
xxd -i -n cube_frag_data "$OUT/cube_frag.sb" \
    shaders/cube_frag_sb.h

