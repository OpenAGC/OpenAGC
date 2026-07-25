/*
 * crt_emu.c — Minimal CRT entry point for emulator builds.
 *
 * The ps5-payload-sdk's crt1.o expects payload_args_t* with a
 * sys_dynlib_dlsym function pointer (for jailbroken PS5 injection).
 * Emulators (KytyPS5, SharpEmu) pass different args to _start, so
 * the CRT bootstrap crashes.
 *
 * This file provides a custom _start that:
 *   1. Ignores the emulator's entry args
 *   2. Calls main() directly
 *   3. Returns (the emulator handles exit)
 *
 * Build with: -nostartfiles crt_emu.c
 */

extern int main(void);

/* Custom entry point — replaces crt1.o's _start.
 * The emulator passes EntryParams* in rdi (or payload_args_t* on real PS5).
 * We ignore it and call main() directly. */
__attribute__((noreturn, visibility("default")))
void _start(void *args) {
    (void)args;  /* ignore emulator/payload args */
    (void)main();
    /* The emulator's return-to-host stub handles exit.
     * __builtin_unreachable tells the compiler no code follows; the
     * emulator catches main()'s return frame at a higher level. */
    __builtin_unreachable();
}
