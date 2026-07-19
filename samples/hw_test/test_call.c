/* Minimal test payload for CMD_PROC_CALL */
#include <stdint.h>

void test_call(volatile uint64_t *trace) {
    if (trace) {
        *trace = 0xDEADBEEFCAFEBABEULL;
        trace[1] = 0x1122334455667788ULL;
    }
}
