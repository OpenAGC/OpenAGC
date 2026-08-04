/*
 * openagc - clean-room gfx1013 compute kernel for sceAgcCbMemsetExclusive.
 *
 * Generated from shaders/memset_exclusive.comp with glslangValidator and
 * openagc-psbc. The AgcShaderRecord is not embedded; only its aligned ACO
 * instruction body is needed at runtime. Source .sb SHA-256:
 * bb9e6a9bec4854b6b5201faacde080c9dd364567df789a360b7238276b375902
 */
#ifndef _OPENAGC_MEMSET_EXCLUSIVE_SHADER_H_
#define _OPENAGC_MEMSET_EXCLUSIVE_SHADER_H_

#include <stdint.h>

#define AGC_MEMSET_EXCLUSIVE_RSRC1 0x602C0000u
#define AGC_MEMSET_EXCLUSIVE_RSRC2 0x00000092u
#define AGC_MEMSET_EXCLUSIVE_RSRC3 0x00000000u

_Alignas(256) static const uint32_t s_agc_memset_exclusive_code[] = {
    0xD7460000u, 0x04010C09u, 0x7DA80004u, 0xBF88000Cu,
    0x3002009Fu, 0x7E080205u, 0xD7000005u, 0x02000C80u,
    0x7E0E0208u, 0xD6FF0000u, 0x02020084u, 0xD70F6A02u,
    0x02020002u, 0x50060203u, 0xDC788000u, 0x007D0402u,
    0xBF810000u, 0xBF9F0000u, 0xBF9F0000u, 0xBF9F0000u,
    0xBF9F0000u, 0xBF9F0000u,
};

_Static_assert(sizeof(s_agc_memset_exclusive_code) == 88,
    "memset-exclusive gfx1013 code size mismatch");

#endif /* _OPENAGC_MEMSET_EXCLUSIVE_SHADER_H_ */
