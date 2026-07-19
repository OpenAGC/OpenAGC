/* Test that CMD_PROC_CALL passes rdi and returns correctly */
#include <stdint.h>

uint64_t test_rdi(uint64_t x) {
    return x + 1;
}
