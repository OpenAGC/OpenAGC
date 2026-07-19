/* colorbars_direct.c — Write color bars directly to the game's framebuffer.
 *
 * This ELF is injected into a running game process. It writes color bars
 * to the game's existing framebuffer in a tight loop, competing with the
 * game's render loop at native speed (no network overhead).
 *
 * The framebuffer address is passed via a global variable that ps5debug-NG
 * can patch before running the ELF. Or we scan for it.
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* The framebuffer address — patch this via ps5debug-NG before running.
 * Default: 0x8fc0000000 (typical for current game) */
volatile uint64_t fb_addr = 0x8fc0000000;
volatile uint32_t fb_width = 3840;
volatile uint32_t fb_height = 2160;

int sceKernelUsleep(unsigned int us);

/* SMPTE color bars (ARGB) */
static const uint32_t COLORS[] = {
    0xFFFFFFFF,  /* White */
    0xFFFFFF00,  /* Yellow */
    0xFF00FFFF,  /* Cyan */
    0xFF00FF00,  /* Green */
    0xFFFF00FF,  /* Magenta */
    0xFFFF0000,  /* Red */
    0xFF0000FF,  /* Blue */
    0xFF000000,  /* Black */
};
#define NUM_COLORS (sizeof(COLORS) / sizeof(COLORS[0]))

void thr_exit(long *state);

void colorbars_main(void) {
    uint32_t *fb = (uint32_t *)(uintptr_t)fb_addr;
    uint32_t w = fb_width;
    uint32_t h = fb_height;
    uint32_t bar_width = w / NUM_COLORS;

    /* Write color bars in a tight loop */
    for (int frame = 0; frame < 600; frame++) {  /* 10 seconds at 60fps */
        for (uint32_t y = 0; y < h; y++) {
            uint32_t *row = fb + y * w;
            for (uint32_t x = 0; x < w; x++) {
                uint32_t bar_idx = x / bar_width;
                if (bar_idx >= NUM_COLORS) bar_idx = NUM_COLORS - 1;
                row[x] = COLORS[bar_idx];
            }
        }
        sceKernelUsleep(16000);  /* ~60fps */
    }

    /* Exit cleanly — don't crash the game */
    long state = 0;
    thr_exit(&state);
    __builtin_unreachable();
}

int main(void) { colorbars_main(); return 0; }

