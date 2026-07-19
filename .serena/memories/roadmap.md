# Roadmap / Next RE Tasks

## Next RE tasks (in order)
1. **PA debug ioctl** — `sceAgcDriverGetPaDebugInterfaceVersion` returns EPERM. Separate kernel permission check (NOT cr_sceAuthId at 0xd8e70400). Needs further kernel RE to identify required capability.
2. **FRAME_OPEN ioctl** — returns EINVAL during init. May need additional context setup. Currently non-blocking (init succeeds without it). NOTE: STATUS.md mentions sce_agc_initialize calls FRAME_OPEN, but `mem:sprx_re` shows the real init is in libSceAgcDriver module_start which does NOT use FRAME_OPEN — it uses CONTEXT_QUERY (0xc004812e) + mmap. FRAME_OPEN (0xc0088100) is NOT handled in FW 5.50 kernel BST.
3. **Validate default state blobs** — confirm primary/internal register-defaults blobs built by `sceAgcDriverNotifyDefaultStates` are accepted by kernel and produce expected GPU state.
4. **Full GPU command submission** — now that queue create, suspend point, and DCB submit work, submit actual rendering commands (draw calls, state setup) via compute queue.
5. **Game compatibility** — continue analyzing game binaries to identify and implement remaining missing AGC functions. See `mem:game_compat`.

## Phase plan (see PLAN.md)
- Phase 0: RE groundwork — mostly implemented
- Phase 1: Packet builder completion — implemented
- Phase 2: Shader records & Wave metadata — partially implemented (parser done; register-block/Wave parsing pending observed evidence)
- Phase 3: Register defaults & state builders — implemented
- Phase 4: /dev/gc ioctl & queue model — implemented
- Phase 5: Native PS5 backend — implemented & built (HW validation remaining gate)
- Phase 6: Higher-level AGC features — mostly speculative:
  - Wave32/Wave64 metadata (no observed offset yet)
  - Geometry/mesh-style processing (no firmware evidence)
  - Ray tracing (no AGC evidence)
  - Cache synchronization (partially observed/implemented)
  - VRS (no register/packet evidence)

## Evidence levels (use in docs/comments)
- **Implemented**: in openagc, host-tested
- **Observed**: in SharpEmu/RPCSX/firmware strings/local analysis, not implemented
- **Inferred**: likely from AMD/RDNA2 behavior, not confirmed in AGC firmware
- **Speculative**: plausible roadmap item, no local evidence

Do not promote inferred/speculative → implemented without packet/struct/register/test/HW artifact.