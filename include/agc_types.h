#ifndef _AGC_TYPES_H_
#define _AGC_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* The PS5 prospero toolchain defines __PROSPERO__.
 * Some older SDKs may define __ORBIS__ instead (PS4 codename). */
#if defined(__PROSPERO__) || defined(__ORBIS__)
#define PS5_SYSV_ABI __attribute__((sysv_abi))
#else
#define PS5_SYSV_ABI
#endif

/*
 * PS5 AGC GPU Mode
 * The PS5 GPU runs in RDNA2 mode (Oberon / custom AMD).
 */
typedef enum {
    kAgcGpuModeBase   = 0,  /* Base mode (similar to PS4 base) */
    kAgcGpuModeNeo    = 1,  /* Enhanced mode (unused on PS5, compat) */
    kAgcGpuModePS5    = 2,  /* Native PS5 mode (RDNA2) */
} AgcGpuMode;

/*
 * Shader stage identifiers for AGC.
 * PS5 uses a unified shader architecture with mesh/amplification shaders.
 */
typedef enum {
    kAgcShaderStageCs  = 0,  /* Compute shader */
    kAgcShaderStagePs  = 1,  /* Pixel shader */
    kAgcShaderStageVs  = 2,  /* Vertex shader */
    kAgcShaderStageGs  = 3,  /* Geometry shader */
    kAgcShaderStageHs  = 4,  /* Hull shader */
    kAgcShaderStageDs  = 5,  /* Domain shader */
    kAgcShaderStageMs  = 6,  /* Mesh shader (new in RDNA2) */
    kAgcShaderStageAs  = 7,  /* Amplification/Task shader (new in RDNA2) */
    kAgcShaderStageCount = 8,
} AgcShaderStage;

/*
 * Primitive topology types for AGC draw commands.
 */
typedef enum {
    kAgcPrimTypeNone           = 0,
    kAgcPrimTypePointList      = 1,
    kAgcPrimTypeLineList       = 2,
    kAgcPrimTypeLineStrip      = 3,
    kAgcPrimTypeTriList        = 4,
    kAgcPrimTypeTriFan         = 5,
    kAgcPrimTypeTriStrip       = 6,
    kAgcPrimTypePatch          = 9,
    kAgcPrimTypeLineListAdj    = 10,
    kAgcPrimTypeLineStripAdj   = 11,
    kAgcPrimTypeTriListAdj     = 12,
    kAgcPrimTypeTriStripAdj    = 13,
    kAgcPrimTypeRectList       = 0x11,
    kAgcPrimTypeLineLoop       = 0x12,
    kAgcPrimTypeQuadList       = 0x13,
    kAgcPrimTypeQuadStrip      = 0x14,
    kAgcPrimTypePolygon        = 0x15,
} AgcPrimitiveType;

/*
 * Index types for indexed draw commands.
 */
typedef enum {
    kAgcIndexSize16 = 0,  /* 16-bit indices */
    kAgcIndexSize32 = 1,  /* 32-bit indices */
} AgcIndexSize;

/*
 * Queue types for AGC command submission.
 * PS5 has separate graphics and compute queues.
 */
typedef enum {
    kAgcQueueGraphics = 0,  /* Graphics (DCB) */
    kAgcQueueCompute  = 1,  /* Async compute (ACB) */
    kAgcQueueCopy     = 2,  /* SDMA / copy queue */
} AgcQueueType;

/*
 * AGC draw flags (equivalent of SceGnmDrawFlags for PS4).
 */
typedef struct {
    uint32_t predication            : 1;
    uint32_t _unused                : 28;
    uint32_t rendertargetsliceoffset : 3;
} AgcDrawFlags;
_Static_assert(sizeof(AgcDrawFlags) == 0x4, "AgcDrawFlags size mismatch");

/*
 * Context state — opaque handle to a GPU context register block.
 * AGC maintains context states for rapid switching.
 */
typedef struct {
    uint32_t data[512];  /* 2048 bytes, sized from firmware strings */
} AgcContextState;

/*
 * Command buffer control block — tracks write position in a DCB/ACB.
 */
typedef struct {
    uint32_t* begin;      /* Start of the command buffer */
    uint32_t* end;        /* End of the allocated space */
    uint32_t* current;    /* Current write position */
    uint32_t  size_dw;    /* Total size in dwords */
} AgcCmdBuffer;

/*
 * Sony-style command buffer control block shape recovered from Gen5 AGC HLE.
 * Cursor-up and cursor-down offsets match HLE reference's command allocation path.
 */
typedef struct {
    uintptr_t reserved0;
    uintptr_t reserved1;
    uintptr_t cursor_up;
    uintptr_t cursor_down;
    uintptr_t callback;
    uintptr_t reserved2;
    uint32_t  reserved_dw;
    uint32_t  reserved3;
} SceAgcCb;
_Static_assert(offsetof(SceAgcCb, cursor_up) == 0x10, "SceAgcCb cursor_up offset mismatch");
_Static_assert(offsetof(SceAgcCb, cursor_down) == 0x18, "SceAgcCb cursor_down offset mismatch");
_Static_assert(offsetof(SceAgcCb, callback) == 0x20, "SceAgcCb callback offset mismatch");
_Static_assert(offsetof(SceAgcCb, reserved_dw) == 0x30, "SceAgcCb reserved_dw offset mismatch");
_Static_assert(sizeof(SceAgcCb) == 0x38, "SceAgcCb size mismatch");

typedef struct {
    uint32_t offset;
    uint32_t value;
} AgcRegisterValue;

typedef struct {
    uintptr_t command_address;
    uint32_t  dword_count;
    uint32_t  reserved;
} AgcCommandBufferSubmit;
_Static_assert(offsetof(AgcCommandBufferSubmit, command_address) == 0x0,
    "AgcCommandBufferSubmit command_address offset mismatch");
_Static_assert(offsetof(AgcCommandBufferSubmit, dword_count) == 0x8,
    "AgcCommandBufferSubmit dword_count offset mismatch");
_Static_assert(sizeof(AgcCommandBufferSubmit) == 0x10,
    "AgcCommandBufferSubmit size mismatch");

/*
 * Workload identifier for AGC workload management.
 */
typedef uint64_t AgcWorkloadId;

/*
 * Suspend point handle for preemption support.
 */
typedef uint64_t AgcSuspendPointHandle;

#endif /* _AGC_TYPES_H_ */
