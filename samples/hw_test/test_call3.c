/* Test write to [rdi] */
#include <stdint.h>

uint64_t test_write(uint64_t *p) {
    *p = 0xDEADBEEFCAFEBABEULL;
    return *p;
}
